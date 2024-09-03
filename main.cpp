#include "chai3d.h"
#include <mutex>
#include <chrono>
#include <vector>
#include <deque>

using namespace chai3d;
using namespace std;

#ifndef MACOSX
#include "GL/glut.h"
#else
#include "GLUT/glut.h"
#endif

// GENERAL SETTINGS
cStereoMode stereoMode = C_STEREO_DISABLED;
bool fullscreen = false;
bool mirroredDisplay = false;

//------------------------------------------------------------------------------
// DECLARED MACROS
//------------------------------------------------------------------------------
#define RESOURCE_PATH(p)    (char*)((resourceRoot+string(p)).c_str())

// DECLARED VARIABLES
cWorld* world;
cCamera* camera;
cHapticDeviceHandler* handler;
cToolCursor* tool;
cGenericHapticDevicePtr hapticDevice;
bool simulationRunning = false;
bool simulationFinished = true;
cFrequencyCounter frequencyCounter;

cDirectionalLight* directionalLight;
cPositionalLight* pointLight;
cSpotLight* spotLight;

void setupLights(cWorld* world) {
	directionalLight = new cDirectionalLight(world);
	world->addChild(directionalLight);
	directionalLight->setEnabled(true);
	directionalLight->setDir(-1.0, -1.0, -1.0);
	directionalLight->m_ambient.set(0.3f, 0.3f, 0.3f);
	directionalLight->m_diffuse.set(0.7f, 0.7f, 0.7f);
	directionalLight->m_specular.set(1.0f, 1.0f, 1.0f);

	// Point light
	pointLight = new cPositionalLight(world);
	world->addChild(pointLight);
	pointLight->setEnabled(true);
	pointLight->setLocalPos(0.0, 2.0, 2.0);
	pointLight->m_ambient.set(0.2f, 0.2f, 0.2f);
	pointLight->m_diffuse.set(0.8f, 0.8f, 0.8f);
	pointLight->m_specular.set(1.0f, 1.0f, 1.0f);
	pointLight->setAttConstant(1.0f);
	pointLight->setAttLinear(0.1f);
	pointLight->setAttQuadratic(0.01f);

	// Spot light
	spotLight = new cSpotLight(world);
	world->addChild(spotLight);
	spotLight->setEnabled(true);
	spotLight->setLocalPos(0.0, -8.0, 3.0);
	spotLight->setDir(1.0, 0.0, -1.0);
	spotLight->m_ambient.set(0.2f, 0.2f, 0.2f);
	spotLight->m_diffuse.set(0.8f, 0.8f, 0.8f);
	spotLight->m_specular.set(1.0f, 1.0f, 1.0f);
	spotLight->setCutOffAngleDeg(30);
	spotLight->setSpotExponent(10);
	spotLight->setAttConstant(1.0f);
	spotLight->setAttLinear(0.1f);
	spotLight->setAttQuadratic(0.01f);
}

void updateLights(double time) {
	// Move the point light in a circular pattern
	pointLight->setLocalPos(2.0 * cos(time), 2.0 * sin(time), 2.0);

	// Change the color of the spot light over time
	float r = (sin(time) + 1.0f) / 2.0f;
	float g = (cos(time) + 1.0f) / 2.0f;
	spotLight->m_diffuse.set(r, g, 0.5f);
}

int screenW, screenH, windowW, windowH, windowPosX, windowPosY;

cMultiMesh* weapon_pistol = nullptr;
cMultiMesh* weapon_dragunov = nullptr;
cMultiMesh* weapon_rifle = nullptr;

cMatrix3d pistolOrientation, dragunovOrientation, rifleOrientation;

bool isPistolLoaded = true;
bool isDragunovLoaded = false;
bool isRifleLoaded = false;

cLabel* weaponNameLabel;

std::vector<cMesh*> blocks;

string resourceRoot;

bool is_pressed;
__int64 time_start;
int elapsed_time;

cVector3d current_force;
cVector3d current_torque;
float deviation_angle;

__int64 coolOfftime = 0;
bool sniperFiring = false;
bool pistolFiring = false;

cVector3d zero_vector(0, 0, 0);

std::mutex deviceMutex;
std::mutex weaponMutex;

volatile bool graphicsUpdateFlag = false;

cVector3d currentToolP;
cVector3d lastToolP;

cShapeLine* forceVector = nullptr;
std::deque<cVector3d> forceHistory;
const int FORCE_HISTORY_SIZE = 100;
const double FORCE_SCALE = 0.1; // Adjust this to scale the force visualization

// Add this function to initialize the force vector
void initForceVisualization(cWorld* world) {
	forceVector = new cShapeLine(cVector3d(0, 0, 0), cVector3d(0, 0, 0));
	forceVector->setLineWidth(2.0);
	forceVector->m_colorPointA.setRed();
	forceVector->m_colorPointB.setRed();
	world->addChild(forceVector);
}

// Add this function to update the force visualization
void updateForceVisualization(const cVector3d& force, const cVector3d& position) {
	cVector3d scaledForce = force * FORCE_SCALE;
	forceVector->m_pointA = position;
	forceVector->m_pointB = position + scaledForce;

	forceHistory.push_front(scaledForce);
	if (forceHistory.size() > FORCE_HISTORY_SIZE) {
		forceHistory.pop_back();
	}
}

// Add this function to draw the force history
void drawForceHistory(cGenericObject* camera) {
	if (forceHistory.empty()) return;

	cVector3d startPos = forceVector->m_pointA;
	glDisable(GL_LIGHTING);
	glBegin(GL_LINE_STRIP);
	for (size_t i = 0; i < forceHistory.size(); ++i) {
		float alpha = 1.0f - static_cast<float>(i) / FORCE_HISTORY_SIZE;
		glColor4f(1.0f, 0.0f, 0.0f, alpha);
		cVector3d point = startPos + forceHistory[i];
		glVertex3d(point.x(), point.y(), point.z());
	}
	glEnd();
	glEnable(GL_LIGHTING);
}

const double CAMERA_SPEED = 0.1;
bool moveForward = false, moveBackward = false, moveLeft = false, moveRight = false;
const double WEAPON_ROTATION_SPEED = 0.002;
bool rotateLeft = false, rotateRight = false;
double currentRotationAngle = 0.0;
const double MAX_ROTATION_ANGLE = cDegToRad(720); // Maximum rotation of 30 degrees in each direction

cShapeLine* bulletTraj;

class CrosshairTarget {
private:
	std::vector<cMesh*> crosshairParts;
	cWorld* world;
	cVector3d position;
	const double MOVEMENT_THRESHOLD = 0.001; // Adjust this value as needed
	const double SMOOTHING_FACTOR = 0.1; // Adjust for more or less smoothing
	std::vector<cVector3d> initialOffsets;

public:
	CrosshairTarget(cWorld* w) : world(w), position(0, 0, 0) {
		createCrosshair();
		storeInitialOffsets();
	}

	void createCrosshair() {
		cMaterial material;
		material.setRed();

		// Center block
		cMesh* center = new cMesh();
		world->addChild(center);
		cCreateBox(center, 0.02, 0.02, 0.02);
		center->setMaterial(material);
		crosshairParts.push_back(center);

		// Top block
		cMesh* top = new cMesh();
		world->addChild(top);
		cCreateBox(top, 0.01, 0.01, 0.05);
		top->setLocalPos(0.0, 0.0, 0.04);
		top->setMaterial(material);
		crosshairParts.push_back(top);

		// Bottom block
		cMesh* bottom = new cMesh();
		world->addChild(bottom);
		cCreateBox(bottom,0.01, 0.01, 0.05);
		bottom->setLocalPos(0.0, 0.0, -0.04);
		bottom->setMaterial(material);
		crosshairParts.push_back(bottom);

		// Left block
		cMesh* left = new cMesh();
		world->addChild(left);
		cCreateBox(left, 0.01, 0.05, 0.01);
		left->setLocalPos(0.0, -0.04, 0.0);
		left->setMaterial(material);
		crosshairParts.push_back(left);

		// Right block
		cMesh* right = new cMesh();
		world->addChild(right);
		cCreateBox(right, 0.01, 0.05, 0.01);
		right->setLocalPos(0.0, 0.04, 0.0);
		right->setMaterial(material);
		crosshairParts.push_back(right);
	}

	void storeInitialOffsets() {
		initialOffsets.clear();
		for (const auto& part : crosshairParts) {
			initialOffsets.push_back(part->getLocalPos());
		}
	}

	void updatePosition(const cVector3d& targetPosition) {
		cVector3d diff = targetPosition - position;
		if (diff.length() > MOVEMENT_THRESHOLD) {
			cVector3d newPosition = position + diff * SMOOTHING_FACTOR;
			setPosition(newPosition);
		}
	}

	void setPosition(const cVector3d& newPosition) {
		position = newPosition;
		for (size_t i = 0; i < crosshairParts.size(); ++i) {
			crosshairParts[i]->setLocalPos(position + initialOffsets[i]);
		}
	}

	cVector3d getPosition() const {
		return crosshairParts[0]->getGlobalPos();
	}
};

CrosshairTarget* crosshair;

class DynamicTarget {
private:
	cMultiMesh* targetMesh;
	cWorld* world;
	double moveInterval;
	double lastMoveTime;
	double initialY;

public:
	DynamicTarget(cWorld* w, double startY) : world(w), moveInterval(3.0), lastMoveTime(0.0), initialY(startY) {
		createTargetShape();
		moveTarget(); // Initial position
	}

	void createTargetShape() {
		targetMesh = new cMultiMesh();
		world->addChild(targetMesh);

		bool fileload;
		fileload = targetMesh->loadFromFile(RESOURCE_PATH("../resources/FinalBaseMesh.obj"));
		if (!fileload) {
#if defined(_MSVC)
			fileload = targetMesh->loadFromFile("../../../bin/resources/FinalBaseMesh.obj");
#endif
		}
		if (!fileload){
			cout << "Error - Target model failed to load correctly." << endl;
			delete targetMesh;
			targetMesh = nullptr;
			return;
		}

		// Scale and set material properties
		targetMesh->scale(0.07);  // Adjust scale as needed
		cMatrix3d rotMat;
		rotMat.identity();
		rotMat.rotateAboutGlobalAxisDeg(1, 0, 0, 90);
		rotMat.rotateAboutGlobalAxisDeg(0, 0, 1, 90);
		targetMesh->setLocalRot(rotMat);
		cMaterial material;
		material.setBlack();
		targetMesh->setMaterial(material);

		// Create a bounding box for the entire mesh
		targetMesh->computeBoundaryBox(true);

		targetMesh->setShowBoundaryBox(false);
	}

	void update(double currentTime) {
		if (currentTime - lastMoveTime >= moveInterval) {
			moveTarget();
			lastMoveTime = currentTime;
		}
	}

	void moveTarget() {
		// Generate random position within the specified bounds
		double x = -4; // Fixed X position
		double y = initialY + ((rand() % 601 - 300) / 100.0);  // Range: initialY - 5 to initialY + 5
		double z = -0.5 + (rand() % 100) / 100.0; // Range: -0.5 to 0.5
		targetMesh->setLocalPos(x, y, z);
	}


	cVector3d getPosition() const {
		return targetMesh->getLocalPos();
	}

	bool checkHit(const cVector3d& weaponPosition, const cVector3d& crosshairPosition) {
		// Calculate ray direction
		cVector3d rayDirection = crosshairPosition - weaponPosition;
		rayDirection.normalize();

		// Get target's bounding box
		cVector3d minBound, maxBound;
		minBound = targetMesh->getBoundaryMin();
		maxBound = targetMesh->getBoundaryMax();

		// Transform bounding box to world coordinates
		cTransform worldTransform = targetMesh->getGlobalTransform();
		minBound = worldTransform * minBound;
		maxBound = worldTransform * maxBound;

		// Check if the ray passes through the AABB
		double t1 = (minBound.x() - weaponPosition.x()) / rayDirection.x();
		double t2 = (maxBound.x() - weaponPosition.x()) / rayDirection.x();
		double t3 = (minBound.y() - weaponPosition.y()) / rayDirection.y();
		double t4 = (maxBound.y() - weaponPosition.y()) / rayDirection.y();
		double t5 = (minBound.z() - weaponPosition.z()) / rayDirection.z();
		double t6 = (maxBound.z() - weaponPosition.z()) / rayDirection.z();

		double tmin = std::max(std::max(std::min(t1, t2), std::min(t3, t4)), std::min(t5, t6));
		double tmax = std::min(std::min(std::max(t1, t2), std::max(t3, t4)), std::max(t5, t6));

		// Ray intersection occurs when tmax > tmin and tmax > 0
		return tmax > std::max(0.0, tmin);
	}

	void moveOnHit(double currentTime) {
		moveTarget();
		lastMoveTime = currentTime;
	}
};

DynamicTarget* dynamicTarget1 = nullptr;
//DynamicTarget* dynamicTarget2 = nullptr;
//DynamicTarget* dynamicTarget3 = nullptr;


bool timeTrialActive = false;
int timeTrialDuration = 30; // 30 seconds
int score = 0;
std::chrono::steady_clock::time_point timeTrialStart;

void updateTimeTrial() {
	if (timeTrialActive) {
		auto currentTime = std::chrono::steady_clock::now();
		int elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(currentTime - timeTrialStart).count();
		if (elapsedSeconds >= timeTrialDuration) {
			timeTrialActive = false;
			cout << "Time's up! Final score: " << score << endl;
			score = 0;  // Reset score for the next trial
		}
	}
}

cLabel* scoreTimeLabel;

void updateBlockTransparency(cWorld* world, cToolCursor* tool)
{
	const double MIN_DISTANCE = 0.5; // Minimum distance before transparency starts
	const double MAX_DISTANCE = 1.5; // Distance at which block becomes fully opaque
	const double MIN_ALPHA = 0.1; // Minimum alpha (maximum transparency)

	cVector3d toolPos = tool->getDeviceGlobalPos();

	for (int i = 0; i < world->getNumChildren(); i++)
	{
		cGenericObject* obj = world->getChild(i);
		cMesh* block = dynamic_cast<cMesh*>(obj);

		if (block && block != tool->m_image) // Check if it's a block and not the weapon
		{
			cVector3d blockPos = block->getGlobalPos();
			double distance = cDistance(blockPos, toolPos);

			if (distance < MIN_DISTANCE)
			{
				block->setTransparencyLevel(MIN_ALPHA);
			}
			else if (distance < MAX_DISTANCE)
			{
				double alpha = MIN_ALPHA + (1.0 - MIN_ALPHA) * ((distance - MIN_DISTANCE) / (MAX_DISTANCE - MIN_DISTANCE));
				block->setTransparencyLevel(alpha);
			}
			else
			{
				block->setTransparencyLevel(1.0); // Fully opaque
			}

			block->setUseTransparency(true);
		}
	}
}

// DECLARED FUNCTIONS
void resizeWindow(int w, int h);
void keySelect(unsigned char key, int x, int y);
void keyRelease(unsigned char key, int x, int y);
void updateGraphics(void);
void updateCameraPosition(void);
void graphicsTimer(int data);
void close(void);
void updateHaptics(void);
__int64 currentTimeMillis();
void applyTextureToWeapon(cMultiMesh* weapon, const std::string& texturePath);
void setInitialWeaponOrientations();
void updateWeaponLabel();
void apply_pistol_force();
void apply_sniper_force();
void apply_rifle_force();
void createBlocks(cWorld* world);
void updateWeaponPositionAndOrientation(cGenericHapticDevicePtr hapticDevice, cToolCursor* tool);
bool checkCollision(const cVector3d& position);

int main(int argc, char* argv[])
{
	srand(static_cast<unsigned int>(time(nullptr)));

	cout << endl;
	cout << "-----------------------------------" << endl;
	cout << "CHAI3D" << endl;
	cout << "OASIS - Shooting Simulator" << endl;
	cout << "-----------------------------------" << endl << endl << endl;
	cout << "Keyboard Options:" << endl << endl;
	cout << "[x] - Exit application" << endl;
	cout << "[w] - forward" << endl;
	cout << "[a] - left" << endl;
	cout << "[s] - right" << endl;
	cout << "[d] - back" << endl;
	cout << "[t] - time trial" << endl;
	cout << endl << endl;

	// OPENGL - WINDOW DISPLAY
	glutInit(&argc, argv);
	screenW = glutGet(GLUT_SCREEN_WIDTH);
	screenH = glutGet(GLUT_SCREEN_HEIGHT);
	windowW = (int)(0.8 * screenH);
	windowH = (int)(0.5 * screenH);
	windowPosY = (screenH - windowH) / 2;
	windowPosX = windowPosY;

	glutInitWindowPosition(windowPosX, windowPosY);
	glutInitWindowSize(windowW, windowH);
	glutInitDisplayMode(GLUT_RGB | GLUT_DEPTH | GLUT_DOUBLE);
	glutCreateWindow(argv[0]);

#ifdef GLEW_VERSION
	glewInit();
#endif

	glutDisplayFunc(updateGraphics);
	glutKeyboardFunc(keySelect);
	glutKeyboardUpFunc(keyRelease);
	glutReshapeFunc(resizeWindow);
	glutSetWindowTitle("CHAI3D");

	if (fullscreen) {
		glutFullScreen();
	}

	// WORLD - CAMERA - LIGHTING
	world = new cWorld();
	world->m_backgroundColor.setWhiteAliceBlue();
	camera = new cCamera(world);
	world->addChild(camera);
	camera->set(cVector3d(5.0, 0.0, 0.0),    // camera position (eye)
		cVector3d(0.0, 0.0, 0.0),    // look at position (target)
		cVector3d(0.0, 0.0, 1.0));   // direction of the (up) vector
	camera->setClippingPlanes(0.01, 100);
	camera->setUseMultipassTransparency(true);

	setupLights(world);
	// initForceVisualization(world);

	// HAPTIC DEVICES / TOOLS
	handler = new cHapticDeviceHandler();
	handler->getDevice(hapticDevice, 0);
	cHapticDeviceInfo hapticDeviceInfo = hapticDevice->getSpecifications();

	tool = new cToolCursor(world);
	world->addChild(tool);
	tool->setHapticDevice(hapticDevice);
	double toolRadius = 0.001;
	tool->setRadius(toolRadius);
	tool->setWorkspaceRadius(1.0);
	tool->setWaitForSmallForce(true);
	tool->start();
	tool->setUseTransparency(true);

	// read the scale factor between the physical workspace of the haptic device and the virtual workspace defined for the tool
	double workspaceScaleFactor = tool->getWorkspaceScaleFactor();

	// properties
	double maxStiffness = hapticDeviceInfo.m_maxLinearStiffness / workspaceScaleFactor;

	// WIDGETS
	cFont *font = NEW_CFONTCALIBRI32();

	scoreTimeLabel = new cLabel(font);
	scoreTimeLabel->m_fontColor.setGreenDarkOlive();
	camera->m_frontLayer->addChild(scoreTimeLabel);
	scoreTimeLabel->setLocalPos(10, windowH + 350);

	// Declare the background pointer
	cBackground* background = new cBackground();

	// Attempt to load the background image
	bool fload = background->loadFromFile(RESOURCE_PATH("../resources/b1.jpg"));
	if (!fload) {
#if defined(_MSVC)
		fload = background->loadFromFile("../../../bin/resources/b1.jpg");
#endif
	}

	// Check if the background loaded successfully
	if (!fload) {
		cout << "Error - Background image failed to load correctly." << endl;
		delete background;
		background = nullptr;
	}
	else {
		// If the background loaded successfully, add it to the camera
		// camera->m_backLayer->addChild(background);
	}

	createBlocks(world);
	// Create the targets with different initial Y positions
	dynamicTarget1 = new DynamicTarget(world, 0.0);  // Starts at Y = 3.0
	//dynamicTarget2 = new DynamicTarget(world, 5.0);  // Starts at Y = 5.0
	//dynamicTarget3 = new DynamicTarget(world, 7.0);  // Starts at Y = 7.0

	weaponNameLabel = new cLabel(font);
	weaponNameLabel->m_fontColor.setGreenDarkOlive();
	weaponNameLabel->setText("Current Weapon: M1911 PISTOL");
	camera->m_frontLayer->addChild(weaponNameLabel);
	weaponNameLabel->setLocalPos(10, 10);

	crosshair = new CrosshairTarget(world);

	// CREATE WEAPONS
	weapon_pistol = new cMultiMesh();
	weapon_dragunov = new cMultiMesh();
	weapon_rifle = new cMultiMesh();

	bool fileload;
	fileload = weapon_pistol->loadFromFile(RESOURCE_PATH("../resources/1911.obj"));
	if (!fileload) {
#if defined(_MSVC)
		fileload = weapon_pistol->loadFromFile("../../../bin/resources/1911.obj");
#endif
	}
	if (!fileload) {
		cout << "Error - Pistol model failed to load correctly." << endl;
		delete weapon_pistol;
		weapon_pistol = nullptr;
		close();
		return (-1);
	}

	fileload = weapon_dragunov->loadFromFile(RESOURCE_PATH("../resources/dragunov.obj"));
	if (!fileload) {
#if defined(_MSVC)
		fileload = weapon_dragunov->loadFromFile("../../../bin/resources/dragunov.obj");
#endif
	}
	if (!fileload) {
		cout << "Error - Dragunov model failed to load correctly." << endl;
		delete weapon_dragunov;
		weapon_dragunov = nullptr;
		close();
		return (-1);
	}

	fileload = weapon_rifle->loadFromFile(RESOURCE_PATH("../resources/ak47.obj"));
	if (!fileload) {
#if defined(_MSVC)
		fileload = weapon_rifle->loadFromFile("../../../bin/resources/ak47.obj");
#endif
	}
	if (!fileload) {
		cout << "Error - Dragunov model failed to load correctly." << endl;
		delete weapon_rifle;
		weapon_rifle = nullptr;
		close();
		return (-1);
	}

	weapon_pistol->scale(0.01);
	weapon_dragunov->scale(0.007);
	weapon_rifle->scale(0.3);

	applyTextureToWeapon(weapon_pistol, "../resources/textures/pistol.png");
	applyTextureToWeapon(weapon_dragunov, "../resources/textures/Texture.png");
	applyTextureToWeapon(weapon_rifle, "../resources/textures/ak47.jpg");

	tool->m_image = weapon_pistol;

	weapon_pistol->setUseCulling(false);
	weapon_dragunov->setUseCulling(false);
	weapon_rifle->setUseCulling(false);

	weapon_pistol->createAABBCollisionDetector(toolRadius);
	weapon_dragunov->createAABBCollisionDetector(toolRadius);
	weapon_rifle->createAABBCollisionDetector(toolRadius);

	weapon_pistol->setStiffness(0.1 * maxStiffness, true);
	weapon_dragunov->setStiffness(0.7 * maxStiffness, true);
	weapon_rifle->setStiffness(0.4 * maxStiffness, true);

	weapon_pistol->setUseDisplayList(true);
	weapon_dragunov->setUseDisplayList(true);
	weapon_rifle->setUseDisplayList(true);

	cVector3d devicePosition;
	hapticDevice->getPosition(devicePosition);
	weapon_pistol->setLocalPos(devicePosition);

	cMaterial weaponMaterial;
	weaponMaterial.m_ambient.set(0.3f, 0.3f, 0.3f);
	weaponMaterial.m_diffuse.set(0.7f, 0.7f, 0.7f);
	weaponMaterial.m_specular.set(0.9f, 0.9f, 0.9f);
	weaponMaterial.setShininess(100.0);

	weapon_pistol->setMaterial(weaponMaterial);
	weapon_dragunov->setMaterial(weaponMaterial);
	weapon_rifle->setMaterial(weaponMaterial);

	bulletTraj = new cShapeLine(tool->getLocalPos(), crosshair->getPosition());
	bulletTraj->setLineWidth(2.0);
	bulletTraj->m_colorPointA.set(0.5, 0.0, 0.0);
	bulletTraj->m_colorPointB.set(1.0, 0.0, 0.0);
	bulletTraj->setShowEnabled(false);
	world->addChild(bulletTraj);

	// START SIMULATION
	simulationFinished = false;

	cThread* hapticsThread = new cThread();
	hapticsThread->start(updateHaptics, CTHREAD_PRIORITY_HAPTICS);

	atexit(close);

	glutTimerFunc(50, graphicsTimer, 0);
	glutMainLoop();

	return (0);
}

//------------------------------------------------------------------------------

void resizeWindow(int w, int h)
{
	windowW = w;
	windowH = h;
}

//------------------------------------------------------------------------------

void keySelect(unsigned char key, int x, int y) {
	switch (key) {
	case 27:
	case 'x':
		close();
		exit(0);
		break;
	case 'w':
		moveForward = true;
		break;
	case 's':
		moveBackward = true;
		break;
	case 'a':
		moveLeft = true;
		break;
	case 'd':
		moveRight = true;
		break;
	case 'q':
		rotateLeft = true;
		break;
	case 'e':
		rotateRight = true;
		break;
	case 't':
		if (!timeTrialActive) {
			timeTrialActive = true;
			timeTrialStart = std::chrono::steady_clock::now();
			score = 0;
			cout << "Time trial started!" << endl;
		}
		break;
	}
}

void keyRelease(unsigned char key, int x, int y) {
	switch (key) {
	case 'w':
		moveForward = false;
		break;
	case 's':
		moveBackward = false;
		break;
	case 'a':
		moveLeft = false;
		break;
	case 'd':
		moveRight = false;
		break;
	case 'q':
		rotateLeft = false;
		break;
	case 'e':
		rotateRight = false;
		break;
	}
}

//------------------------------------------------------------------------------

void close(void)
{
	simulationRunning = false;
	while (!simulationFinished) { cSleepMs(100); }
}

//------------------------------------------------------------------------------

void graphicsTimer(int data)
{
	if (simulationRunning)
	{
		glutPostRedisplay();
	}
	glutTimerFunc(50, graphicsTimer, 0);
}

//------------------------------------------------------------------------------

void updateGraphics(void)
{
	// Lock mutexes to ensure consistent state
	std::lock_guard<std::mutex> deviceLock(deviceMutex);
	std::lock_guard<std::mutex> weaponLock(weaponMutex);

	// Check if update is needed
	if (!graphicsUpdateFlag) {
		return;
	}

	updateCameraPosition();

	if (timeTrialActive) {
		auto currentTime = std::chrono::steady_clock::now();
		int elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(currentTime - timeTrialStart).count();
		int remainingTime = timeTrialDuration - elapsedSeconds;
		scoreTimeLabel->setText("Score: " + cStr(score) + " | Time: " + cStr(remainingTime) + "s");
	}
	else {
		scoreTimeLabel->setText("Press 'T' to start time trial");
	}

	// update shadow maps (if any)
	world->updateShadowMaps(false, mirroredDisplay);

	// render world
	camera->renderView(windowW, windowH);

	drawForceHistory(camera);

	// swap buffers
	glutSwapBuffers();

	// wait until all GL commands are completed
	glFinish();

	// check for any OpenGL errors
	GLenum err;
	err = glGetError();
	if (err != GL_NO_ERROR) cout << "Error:  %s\n" << gluErrorString(err);
}

// Update the updateCameraPosition function
void updateCameraPosition() {
	cVector3d pos = camera->getLocalPos();
	cVector3d dir = camera->getLookVector();
	cVector3d right = camera->getRightVector();
	cVector3d newPos = pos;
	cVector3d weaponPos = tool->getLocalPos();

	if (moveForward)
		newPos += dir * CAMERA_SPEED;
	if (moveBackward)
		newPos -= dir * CAMERA_SPEED;
	if (moveLeft)
		newPos -= right * CAMERA_SPEED;
	if (moveRight)
		newPos += right * CAMERA_SPEED;

	// Check for collision before updating position
	if (!checkCollision(newPos)) {
		camera->setLocalPos(newPos);
	}

	updateBlockTransparency(world, tool);
}

//------------------------------------------------------------------------------

void updateHaptics(void)
{
	simulationRunning = true;
	simulationFinished = false;

	auto lastUpdate = std::chrono::high_resolution_clock::now();
	const std::chrono::milliseconds updatePeriod(1);

	while (simulationRunning)
	{
		auto now = std::chrono::high_resolution_clock::now();
		if (now - lastUpdate >= updatePeriod)
		{
			{
				std::lock_guard<std::mutex> deviceLock(deviceMutex);
				std::lock_guard<std::mutex> weaponLock(weaponMutex);

				// Get current time in seconds
				double currentTime = std::chrono::duration_cast<std::chrono::duration<double>>(
					std::chrono::high_resolution_clock::now().time_since_epoch()
					).count();

				world->computeGlobalPositions(true);
				tool->updateFromDevice();

				updateWeaponPositionAndOrientation(hapticDevice, tool);

				cVector3d toolP;
				currentToolP = tool->getDeviceGlobalPos();
				cVector3d toolMovement = currentToolP - lastToolP;
				cVector3d toolMovementDirection;
				toolMovement.normalizer(toolMovementDirection);

				if (toolMovement.y() > 0){
					currentToolP.add(cVector3d(0, toolMovement.y()*toolMovementDirection.y(), 0));
				}

				if (isPistolLoaded){
					cVector3d targetPos = currentToolP + cVector3d(-2.0, 0, 0.1);
					crosshair->setPosition(targetPos);
				}
				else if (isRifleLoaded){
					cVector3d targetPos = currentToolP + cVector3d(-2.0, 0, 0.5);
					crosshair->setPosition(targetPos);
				}
				else {
					cVector3d targetPos = currentToolP + cVector3d(-2.0, 0, 0.0);
					crosshair->setPosition(targetPos);
				}

				dynamicTarget1->update(currentTime);
				//dynamicTarget2->update(currentTime);
				//dynamicTarget3->update(currentTime);

				updateLights(currentTime);
				updateBlockTransparency(world, tool);
			}

			if (is_pressed) {
				elapsed_time = currentTimeMillis() - time_start;
			}
			else {
				elapsed_time = 0;
			}

			bool button0, button1, button2, button3;
			hapticDevice->getUserSwitch(0, button0);
			hapticDevice->getUserSwitch(1, button1);
			hapticDevice->getUserSwitch(2, button2);
			hapticDevice->getUserSwitch(3, button3);

			if (sniperFiring) {
				is_pressed = true;
				button0 = true;
			}

			if (pistolFiring) {
				is_pressed = true;
				button0 = true;
			}

			if (!is_pressed && button0) {
				is_pressed = true;
				time_start = currentTimeMillis();
			}

			cVector3d weaponPosition = tool->getDeviceGlobalPos();
			cVector3d crosshairPosition = crosshair->getPosition();

			// Get current time
			double currentTime = std::chrono::duration_cast<std::chrono::duration<double>>(
				std::chrono::high_resolution_clock::now().time_since_epoch()
				).count();

			if (is_pressed && button0) {
				if (isPistolLoaded) {
					apply_pistol_force();
				}
				else if (isRifleLoaded) {
					apply_rifle_force();
				}
				else if (isDragunovLoaded) {
					apply_sniper_force();
				}
				if (dynamicTarget1->checkHit(weaponPosition, crosshairPosition)) {
					// Handle hit on target 1
					dynamicTarget1->moveOnHit(currentTime);
					std::cout << "Hit!" << std::endl;

					if (timeTrialActive) {
						score++;
					}
				}
				//if (dynamicTarget2->checkHit(weaponPosition, crosshairPosition)) {
				//	// Handle hit on target 2
				//	dynamicTarget2->moveOnHit(currentTime);
				//	if (timeTrialActive) {
				//		std::cout << "Hit detected!" << std::endl;
				//		score++;
				//		cout << "Hit! Current score: " << score << endl;
				//	}
				//}
				//if (dynamicTarget3->checkHit(weaponPosition, crosshairPosition)) {
				//	// Handle hit on target 3
				//	dynamicTarget3->moveOnHit(currentTime);
				//	if (timeTrialActive) {
				//		std::cout << "Hit detected!" << std::endl;
				//		score++;
				//		cout << "Hit! Current score: " << score << endl;
				//	}
				//}
			}

			if (!(is_pressed && button0)) {
				hapticDevice->setForce(zero_vector);
				bulletTraj->setShowEnabled(false);
			}

			if (is_pressed && !button0) {
				hapticDevice->setForce(zero_vector);
				is_pressed = false;
			}

			if (button1 && !isPistolLoaded) {
				tool->m_image = weapon_pistol;
				isPistolLoaded = true;
				isDragunovLoaded = false;
				isRifleLoaded = false;
				updateWeaponLabel();
			}
			else if (button2 && !isRifleLoaded) {
				tool->m_image = weapon_rifle;
				isPistolLoaded = false;
				isDragunovLoaded = false;
				isRifleLoaded = true;
				updateWeaponLabel();
			}
			else if (button3 && !isDragunovLoaded) {
				tool->m_image = weapon_dragunov;
				isPistolLoaded = false;
				isDragunovLoaded = true;
				isRifleLoaded = false;
				updateWeaponLabel();
			}

			updateTimeTrial();

			{
				std::lock_guard<std::mutex> deviceLock(deviceMutex);
				std::lock_guard<std::mutex> weaponLock(weaponMutex);

				tool->computeInteractionForces();
				lastToolP = currentToolP;
			}

			graphicsUpdateFlag = true;
			lastUpdate = now;
		}
	}
	simulationFinished = true;
}
//------------------------------------------------------------------------------

__int64 currentTimeMillis() {
	FILETIME f;
	GetSystemTimeAsFileTime(&f);
	(long long)f.dwHighDateTime;
	__int64 nano = ((__int64)f.dwHighDateTime << 32LL) + (__int64)f.dwLowDateTime;
	return (nano - 116444736000000000LL) / 10000;
}

//------------------------------------------------------------------------------

void applyTextureToWeapon(cMultiMesh* weapon, const std::string& texturePath) {
	cTexture2dPtr weaponTexture = cTexture2d::create();
	bool fileload = weaponTexture->loadFromFile(RESOURCE_PATH(texturePath.c_str()));
	if (!fileload) {
#if defined(_MSVC)
		fileload = weaponTexture->loadFromFile((std::string("../../../bin/resources/") + texturePath).c_str());
#endif
	}
	if (!fileload) {
		cout << "Error - Texture file failed to load correctly: " << texturePath << endl;
		return;
	}

	int numMeshes = weapon->getNumMeshes();
	for (int i = 0; i < numMeshes; i++) {
		cMesh* mesh = weapon->getMesh(i);
		if (mesh != nullptr) {
			mesh->setTexture(weaponTexture);
			mesh->setUseTexture(true);
		}
	}
}
//------------------------------------------------------------------------------

void createBlocks(cWorld* world) {
	for (int i = 0; i < 5; i++) {
		for (int j = 0; j < 5; j++) {
			cMesh* block = new cMesh();
			world->addChild(block);
			cCreateBox(block, 0.0, 0.0, 0.0);//  +(rand() % 100) / 100.0);
			block->setLocalPos(i * 1.0 - 2.0, j * 1.0 - 2.0, 0.0);
			cMaterial material;
			material.setBlueDeepSky();
			block->setMaterial(material);
			block->setUseTransparency(true);
			blocks.push_back(block);
		}
	}
}

bool checkCollision(const cVector3d& position) {
	for (const auto& block : blocks) {
		cVector3d blockPos = block->getLocalPos();
		cVector3d blockSize(0.5, 0.5, 0.5);  // Assuming blocks are 0.5 units in each dimension

		// Check if position is within the block's bounds
		if (position.x() >= blockPos.x() - blockSize.x() / 2 && position.x() <= blockPos.x() + blockSize.x() / 2 &&
			position.y() >= blockPos.y() - blockSize.y() / 2 && position.y() <= blockPos.y() + blockSize.y() / 2 &&
			position.z() >= blockPos.z() - blockSize.z() / 2 && position.z() <= blockPos.z() + blockSize.z() / 2) {
			return true;  // Collision detected
		}
	}
	return false;  // No collision
}


//------------------------------------------------------------------------------

void setInitialWeaponOrientations(void) {
	// Pistol orientation
	pistolOrientation.identity();
	pistolOrientation.rotateAboutGlobalAxisDeg(1, 0, 0, 90); // - up + down
	pistolOrientation.rotateAboutGlobalAxisDeg(0, 1, 0, 0);
	pistolOrientation.rotateAboutGlobalAxisDeg(0, 0, 1, -90); // was -90
	weapon_pistol->setLocalRot(pistolOrientation);

	// Dragunov orientation
	dragunovOrientation.identity();
	dragunovOrientation.rotateAboutGlobalAxisDeg(1, 0, 0, 90); // lean right left (- right + left)
	dragunovOrientation.rotateAboutGlobalAxisDeg(0, 1, 0, 0); // + up - down 
	dragunovOrientation.rotateAboutGlobalAxisDeg(0, 0, 1, 0); // was 0 
	weapon_dragunov->setLocalRot(dragunovOrientation);

	// Rifle orientation
	rifleOrientation.identity();
	rifleOrientation.rotateAboutGlobalAxisDeg(1, 0, 0, 180); 
	rifleOrientation.rotateAboutGlobalAxisDeg(0, 1, 0, 180); // + up - down
	rifleOrientation.rotateAboutGlobalAxisDeg(0, 0, 1, 0); // was 0
	weapon_rifle->setLocalRot(rifleOrientation);
}

//------------------------------------------------------------------------------

void updateWeaponPositionAndOrientation(cGenericHapticDevicePtr hapticDevice, cToolCursor* tool) {
	setInitialWeaponOrientations();

	// Get camera position and orientation
	cVector3d camPosition = camera->getLocalPos();

	// Get the camera's direction
	cVector3d cameraDir = camera->getLookVector();
	cameraDir.normalize();

	// Define a fixed offset for the weapon in camera space
	cVector3d weaponOffset = cVector3d(-2.0, 0, 0);  // Adjust these values as needed

	// Calculate weapon position in world space
	cVector3d weaponPosition = camPosition + weaponOffset;

	cVector3d offsetPos = weaponPosition + cameraDir * 0.1;  // Adjust 0.1 as needed

	tool->setLocalPos(offsetPos);

	// Apply weapon rotation
	if (rotateLeft && currentRotationAngle > -MAX_ROTATION_ANGLE) {
		currentRotationAngle -= WEAPON_ROTATION_SPEED;
		currentRotationAngle = cMax(currentRotationAngle, -MAX_ROTATION_ANGLE);
	}
	if (rotateRight && currentRotationAngle < MAX_ROTATION_ANGLE) {
		currentRotationAngle += WEAPON_ROTATION_SPEED;
		currentRotationAngle = cMin(currentRotationAngle, MAX_ROTATION_ANGLE);
	}

	// Create rotation matrix based on the current rotation angle
	cMatrix3d rotZ;
	rotZ.identity();
	rotZ.rotateAboutLocalAxisRad(cVector3d(0, 1, 0), currentRotationAngle);
	cMatrix3d rotZR;
	rotZR.identity();
	rotZR.rotateAboutLocalAxisRad(cVector3d(0, 0, 1), currentRotationAngle);

	// Apply the rotation to the current weapon
	cMatrix3d currentWeaponRot;
	if (isPistolLoaded) {
		currentWeaponRot = pistolOrientation * rotZ;
		weapon_pistol->setLocalRot(currentWeaponRot);
	}
	else if (isDragunovLoaded) {
		currentWeaponRot = dragunovOrientation * rotZ;
		weapon_dragunov->setLocalRot(currentWeaponRot);
	}
	else if (isRifleLoaded) {
		currentWeaponRot = rifleOrientation * rotZR;
		weapon_rifle->setLocalRot(currentWeaponRot);
	}

	// Update crosshair position based on weapon rotation
	cVector3d crosshairOffset = cVector3d(-2.0, 0, 0); // Adjust as needed
	crosshairOffset = rotZ * crosshairOffset;
	cVector3d newCrosshairPos = offsetPos + crosshairOffset;
	crosshair->setPosition(newCrosshairPos);
}

//------------------------------------------------------------------------------

void updateWeaponLabel() {
	if (isPistolLoaded) {
		weaponNameLabel->setText("M1911");
	}
	else if (isDragunovLoaded) {
		weaponNameLabel->setText("DRAGUNOV");
	}
	else if (isRifleLoaded) {
		weaponNameLabel->setText("AK47");
	}
}

//------------------------------------------------------------------------------

void apply_pistol_force(void) {
	std::lock_guard<std::mutex> deviceLock(deviceMutex);
	std::lock_guard<std::mutex> weaponLock(weaponMutex);

	float mf = 1.1;  // mass of firearm
	float vf = 3.978;  // velocity of firearm
	float mb = 0.015;  // mass of bullet 
	float barrel_length = 0.127;  // barrel length
	float tr = 0.003;  // recoil time
	float force = 0.2 * (vf / tr);  // Increased force multiplier from 0.15 to 0.2

	cVector3d direction(1 + ((rand() % 20) - 10) / 100.0,
		((rand() % 20) - 10) / 100.0,
		0.3 + ((rand() % 20) - 10) / 100.0);
	direction.normalize();
	cVector3d initial_force = force * direction;

	float h_axis = 0.0678;
	cVector3d initial_torque = h_axis * initial_force;
	float moment_of_inertia = (h_axis * h_axis) * mf;
	float deviation_angle = (h_axis * mb * barrel_length) / moment_of_inertia;

	const int recoil_duration = 50;  // ms
	const int recovery_duration = 100; // ms
	const int total_duration = recoil_duration + recovery_duration;
	
	if (elapsed_time < total_duration) {
		pistolFiring = true;
		cVector3d current_force;
		cVector3d current_torque;

		if (elapsed_time < recoil_duration) {
			// Recoil phase
			float recoil_progress = (float)elapsed_time / recoil_duration;
			float decay_factor = exp(-5.0 * recoil_progress);  // Faster decay
			current_force = initial_force * decay_factor;
			current_torque = initial_torque * decay_factor * deviation_angle;
		}
		else {
			// Recovery phase
			float recovery_progress = (float)(elapsed_time - recoil_duration) / recovery_duration;
			float recovery_factor = exp(-5.0 * recovery_progress) * 0.3;  // Faster recovery
			current_force = -initial_force * recovery_factor;
			current_torque = -initial_torque * recovery_factor * deviation_angle;
		}

		// Apply the calculated force and torque
		hapticDevice->setForceAndTorque(current_force, current_torque);
		// updateForceVisualization(current_force, tool->getDeviceGlobalPos());

		cVector3d weaponPosi = tool->getDeviceGlobalPos();
		weaponPosi.add(cVector3d(0.0, 0, 0.1));
		bulletTraj->m_pointA = weaponPosi;
		bulletTraj->m_pointB = crosshair->getPosition() + cVector3d(-10,0,0);
		bulletTraj->setShowEnabled(true);

		// Visual feedback: upward and slight sideways rotation
		float max_vertical_recoil_angle = 15.0; // Maximum vertical recoil angle in degrees
		float max_horizontal_recoil_angle = 3.0; // Maximum horizontal recoil angle in degrees
		float current_vertical_angle;
		float current_horizontal_angle;

		const int visual_recoil_duration = 30; // ms, faster visual recoil
		const int visual_recovery_duration = 50; // ms, faster visual recovery
		const int visual_total_duration = visual_recoil_duration + visual_recovery_duration;

		if (elapsed_time < visual_recoil_duration) {
			// Fast, linear upward and sideways rotation during recoil
			float progress = (float)elapsed_time / visual_recoil_duration;
			current_vertical_angle = max_vertical_recoil_angle * progress;
			current_horizontal_angle = max_horizontal_recoil_angle * progress * (rand() % 2 == 0 ? 1 : -1); // Random left or right
		}
		else if (elapsed_time < visual_total_duration) {
			// Fast, linear return to original position during recovery
			float progress = (float)(elapsed_time - visual_recoil_duration) / visual_recovery_duration;
			current_vertical_angle = max_vertical_recoil_angle * (1.0 - progress);
			current_horizontal_angle = max_horizontal_recoil_angle * (1.0 - progress) * (rand() % 2 == 0 ? 1 : -1); // Random left or right
		}
		else {
			// Hold at original position for the remainder of the haptic feedback
			current_vertical_angle = 0;
			current_horizontal_angle = 0;
		}

		cMatrix3d pistolRecoil;
		pistolRecoil.identity();
		pistolRecoil.rotateAboutLocalAxisDeg(cVector3d(1, 0, 0), -current_vertical_angle); // Vertical recoil
		pistolRecoil.rotateAboutLocalAxisDeg(cVector3d(0, 1, 0), current_horizontal_angle); // Horizontal recoil

		// Apply the rotation to the weapon's current orientation
		cMatrix3d currentRot = weapon_pistol->getLocalRot();
		weapon_pistol->setLocalRot(currentRot * pistolRecoil);
	}
	else {
		pistolFiring = false;
		hapticDevice->setForce(zero_vector);
		// Reset weapon rotation
		weapon_pistol->setLocalRot(weapon_pistol->getLocalRot());
		bulletTraj->setShowEnabled(false);
	}
}

//------------------------------------------------------------------------------

void apply_rifle_force(void) {
	float mf = 3.9;  // mass of firearm
	float vf = 2.2688;  // velocity of firearm
	float mb = 0.0079;  // mass of bullet 
	float barrel_length = 0.415;  // barrel length
	float tr = 0.06;  // recoil time
	float force = 0.15 * (vf / tr);

	cVector3d direction(1 + ((rand() % 20) - 10) / 100.0,
		((rand() % 20) - 10) / 100.0,
		0.3 + ((rand() % 20) - 10) / 100.0);
	direction.normalize();
	cVector3d current_force = force * direction * 100;

	float h_axis = 0.065;
	cVector3d current_torque = h_axis * current_force;
	float moment_of_inertia = (h_axis * h_axis) * mf;
	float deviation_angle = (h_axis * mb * barrel_length) / moment_of_inertia;

	if (elapsed_time < 60) {
		// apply the calculated force and torque
		hapticDevice->setForceAndTorque(current_force, current_torque * deviation_angle);
		// updateForceVisualization(current_force, tool->getDeviceGlobalPos());

		cVector3d weaponPosi = tool->getDeviceGlobalPos();
		weaponPosi.add(cVector3d(0.0, 0, 0.5));
		bulletTraj->m_pointA = weaponPosi;
		bulletTraj->m_pointB = crosshair->getPosition() + cVector3d(-10, 0, 0);
		bulletTraj->setShowEnabled(true);

		// Visual feedback
		float max_vertical_recoil_angle = 5.0; // Maximum vertical recoil angle in degrees (smaller for continuous fire)
		float max_horizontal_recoil_angle = 1.5; // Maximum horizontal recoil angle in degrees

		// Calculate current recoil angles
		float vertical_recoil = max_vertical_recoil_angle * (1.0 - (float)elapsed_time / 60.0);
		float horizontal_recoil = max_horizontal_recoil_angle * sin((float)elapsed_time / 60.0 * M_PI) * (rand() % 2 == 0 ? 1 : -1);

		cMatrix3d rifleRecoil;
		rifleRecoil.identity();
		rifleRecoil.rotateAboutLocalAxisDeg(cVector3d(0, 1, 0), -vertical_recoil); // Vertical recoil
		rifleRecoil.rotateAboutLocalAxisDeg(cVector3d(1, 0, 0), horizontal_recoil); // Horizontal recoil

		// Apply the rotation to the weapon's current orientation
		cMatrix3d currentRot = weapon_rifle->getLocalRot();
		weapon_rifle->setLocalRot(currentRot * rifleRecoil);
	}
	else if (elapsed_time < 120) {
		hapticDevice->setForce(zero_vector);
		//updateForceVisualization(current_force, tool->getDeviceGlobalPos());

		// Visual recovery
		float recovery_progress = (float)(elapsed_time - 60) / 60.0;
		cMatrix3d rifleRecovery;
		rifleRecovery.identity();
		rifleRecovery.rotateAboutLocalAxisDeg(cVector3d(1, 0, 0), 3.0 * recovery_progress); // Vertical recovery

		cMatrix3d currentRot = weapon_rifle->getLocalRot();
		weapon_rifle->setLocalRot(currentRot * rifleRecovery);
	}
	else {
		time_start = currentTimeMillis();
		elapsed_time = 0;
		// Reset weapon rotation
		weapon_rifle->setLocalRot(weapon_rifle->getLocalRot());
		bulletTraj->setShowEnabled(false);
	}
}

//------------------------------------------------------------------------------

void apply_sniper_force(void) {
	std::lock_guard<std::mutex> deviceLock(deviceMutex);
	std::lock_guard<std::mutex> weaponLock(weaponMutex);
	float mf = 4.3;	// mass of firearm
	float vf = 3.265;	// velocity of firearm
	float mb = 0.0113;	// mass of bullet 
	float barrel_length = 0.62;	// barrel length
	float tr = 0.005;	// reduced recoil time for higher initial force
	float force = 0.15 * (vf / tr);

	cVector3d direction(1 + ((rand() % 20) - 10) / 100.0, ((rand() % 20) - 10) / 100.0, 0.3 + ((rand() % 20) - 10) / 100.0);
	direction.normalize();

	cVector3d initial_force = force * direction * 5.0; // Multiplied by 5 for very high initial force
	float h_axis = 0.045;
	cVector3d initial_torque = h_axis * initial_force;
	float moment_of_inertia = (h_axis*h_axis)*mf;
	float deviation_angle = (h_axis*mb*barrel_length) / moment_of_inertia;

	const int recoil_duration = 120;  // ms
	const int recovery_duration = 300; // ms
	const int total_duration = recoil_duration + recovery_duration;

	if (elapsed_time < total_duration) {
		sniperFiring = true;
		cVector3d current_force;
		cVector3d current_torque;

		if (elapsed_time < recoil_duration) {
			// Recoil phase
			float recoil_progress = (float)elapsed_time / recoil_duration;
			float decay_factor = exp(-3.0 * recoil_progress);
			current_force = initial_force * decay_factor;
			current_torque = initial_torque * decay_factor * deviation_angle;
		}
		else {
			// Recovery phase
			float recovery_progress = (float)(elapsed_time - recoil_duration) / recovery_duration;
			float recovery_factor = exp(-3.0 * recovery_progress) * 0.2;
			current_force = -initial_force * recovery_factor;
			current_torque = -initial_torque * recovery_factor * deviation_angle;
		}

		// Apply the calculated force and torque
		hapticDevice->setForceAndTorque(current_force, current_torque);
		//updateForceVisualization(current_force, tool->getDeviceGlobalPos());

		cVector3d weaponPosi = tool->getDeviceGlobalPos();
		weaponPosi.add(cVector3d(0.0, 0, 0.0));
		bulletTraj->m_pointA = weaponPosi;
		bulletTraj->m_pointB = crosshair->getPosition() + cVector3d(-10, 0, 0);
		bulletTraj->setShowEnabled(true);

		// Visual feedback: simple upward rotation
		float max_recoil_angle = 25.0; // Maximum recoil angle in degrees
		float current_angle;

		if (elapsed_time < recoil_duration) {
			// Smooth upward rotation during recoil
			current_angle = max_recoil_angle * (float)elapsed_time / recoil_duration;
		}
		else {
			// Smooth downward rotation during recovery
			current_angle = max_recoil_angle * (1.0 - (float)(elapsed_time - recoil_duration) / recovery_duration);
		}

		cMatrix3d sniperRecoil;
		sniperRecoil.identity();
		sniperRecoil.rotateAboutLocalAxisDeg(cVector3d(0, 0, 1), -current_angle); // Rotate around x-axis

		// Apply the rotation to the weapon's current orientation
		cMatrix3d currentRot = weapon_dragunov->getLocalRot();
		weapon_dragunov->setLocalRot(currentRot * sniperRecoil);
	}
	else {
		sniperFiring = false;
		hapticDevice->setForce(zero_vector);
		// Reset weapon rotation
		weapon_dragunov->setLocalRot(weapon_dragunov->getLocalRot());
		bulletTraj->setShowEnabled(false);
	}
}
