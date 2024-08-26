#include "chai3d.h"
#include <mutex>
#include <chrono>

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

// DECLARED VARIABLES
cWorld* world;
cCamera* camera;
cDirectionalLight *light;
cHapticDeviceHandler* handler;
cToolCursor* tool;
cGenericHapticDevicePtr hapticDevice;
cLabel* labelHapticRate;
bool simulationRunning = false;
bool simulationFinished = true;
cFrequencyCounter frequencyCounter;

int screenW, screenH, windowW, windowH, windowPosX, windowPosY;

cMultiMesh* weapon_pistol = nullptr;
cMultiMesh* weapon_dragunov = nullptr;
cMultiMesh* weapon_rifle = nullptr;

cMatrix3d pistolOrientation, dragunovOrientation, rifleOrientation;

bool isPistolLoaded = true;
bool isDragunovLoaded = false;
bool isRifleLoaded = false;

cLabel* weaponNameLabel;

string resourceRoot;

bool is_pressed;
__int64 time_start;
int elapsed_time;

cVector3d current_force;
cVector3d current_torque;
float deviation_angle;

__int64 coolOfftime = 0;
bool sniperFiring = false;

cVector3d zero_vector(0, 0, 0);

std::mutex deviceMutex;
std::mutex weaponMutex;

volatile bool graphicsUpdateFlag = false;

const double CAMERA_SPEED = 0.1;
bool moveForward = false, moveBackward = false, moveLeft = false, moveRight = false;
//------------------------------------------------------------------------------
// DECLARED MACROS
//------------------------------------------------------------------------------
#define RESOURCE_PATH(p)    (char*)((resourceRoot+string(p)).c_str())

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

int main(int argc, char* argv[])
{
	cout << endl;
	cout << "-----------------------------------" << endl;
	cout << "CHAI3D" << endl;
	cout << "OASIS - Shooting Simulator" << endl;
	cout << "-----------------------------------" << endl << endl << endl;
	cout << "Keyboard Options:" << endl << endl;
	cout << "[x] - Exit application" << endl;
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
	world->m_backgroundColor.setWhite();
	camera = new cCamera(world);
	world->addChild(camera);
	camera->set(cVector3d(5.0, 0.0, 0.0),    // camera position (eye)
		cVector3d(0.0, 0.0, 0.0),    // look at position (target)
		cVector3d(0.0, 0.0, 1.0));   // direction of the (up) vector
	camera->setClippingPlanes(0.01, 100);

	light = new cDirectionalLight(world);
	world->addChild(light);
	light->setEnabled(true);
	light->setDir(-1.0, -1.0, -1.0);
	light->m_ambient.set(0.4f, 0.4f, 0.4f);
	light->m_diffuse.set(0.8f, 0.8f, 0.8f);
	light->m_specular.set(1.0f, 1.0f, 1.0f);

	// HAPTIC DEVICES / TOOLS
	handler = new cHapticDeviceHandler();
	handler->getDevice(hapticDevice, 0);
	cHapticDeviceInfo hapticDeviceInfo = hapticDevice->getSpecifications();

	tool = new cToolCursor(world);
	world->addChild(tool);
	tool->setHapticDevice(hapticDevice);
	double toolRadius = 0.01;
	tool->setRadius(toolRadius);
	tool->setWorkspaceRadius(1.0);
	tool->setWaitForSmallForce(true);
	tool->start();
	tool->setUseTransparency(true);
	tool->m_material->setRed();

	// read the scale factor between the physical workspace of the haptic device and the virtual workspace defined for the tool
	double workspaceScaleFactor = tool->getWorkspaceScaleFactor();

	// properties
	double maxStiffness = hapticDeviceInfo.m_maxLinearStiffness / workspaceScaleFactor;

	// WIDGETS
	cFont *font = NEW_CFONTCALIBRI32();
	labelHapticRate = new cLabel(font);
	labelHapticRate->m_fontColor.setWhiteAntique();
	camera->m_frontLayer->addChild(labelHapticRate);
	labelHapticRate->setLocalPos(windowW - 100, 10);

	createBlocks(world);

	weaponNameLabel = new cLabel(font);
	weaponNameLabel->m_fontColor.setGreenDarkOlive();
	weaponNameLabel->setText("Current Weapon: M1911 PISTOL");
	camera->m_frontLayer->addChild(weaponNameLabel);
	weaponNameLabel->setLocalPos(10, 10);

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

	// set materials for the weapons
	cMaterial mat;
	weapon_pistol->setMaterial(mat);
	weapon_dragunov->setMaterial(mat);
	weapon_rifle->setMaterial(mat);

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

	labelHapticRate->setText(cStr(frequencyCounter.getFrequency(), 0) + " Hz");

	// update shadow maps (if any)
	world->updateShadowMaps(false, mirroredDisplay);

	// render world
	camera->renderView(windowW, windowH);

	// swap buffers
	glutSwapBuffers();

	// wait until all GL commands are completed
	glFinish();

	// check for any OpenGL errors
	GLenum err;
	err = glGetError();
	if (err != GL_NO_ERROR) cout << "Error:  %s\n" << gluErrorString(err);
}

void updateCameraPosition() {
	cVector3d pos = camera->getLocalPos();
	cVector3d dir = camera->getLookVector();
	cVector3d right = camera->getRightVector();

	if (moveForward)
		pos += dir * CAMERA_SPEED;
	if (moveBackward)
		pos -= dir * CAMERA_SPEED;
	if (moveLeft)
		pos -= right * CAMERA_SPEED;
	if (moveRight)
		pos += right * CAMERA_SPEED;

	camera->setLocalPos(pos);
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
				
				world->computeGlobalPositions(true);
				tool->updateFromDevice();

				updateWeaponPositionAndOrientation(hapticDevice, tool);
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

			if (!is_pressed && button0) {
				is_pressed = true;
				time_start = currentTimeMillis();
			}

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
			}

			if (!(is_pressed && button0)) {
				hapticDevice->setForce(zero_vector);
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

			{
				std::lock_guard<std::mutex> deviceLock(deviceMutex);
				std::lock_guard<std::mutex> weaponLock(weaponMutex);

				tool->computeInteractionForces();
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

			cCreateBox(block, 0.5, 0.5, 0.5);
			block->setLocalPos(i * 1.0 - 2.0, j * 1.0 - 2.0, 0.0);

			cMaterial material;
			material.setGrayLevel(0.5);
			block->setMaterial(material);
		}
	}
}

//------------------------------------------------------------------------------

void setInitialWeaponOrientations(void) {
	// Pistol orientation
	pistolOrientation.identity();
	pistolOrientation.rotateAboutGlobalAxisDeg(1, 0, 0, 90); // - up + down
	pistolOrientation.rotateAboutGlobalAxisDeg(0, 1, 0, 0);
	pistolOrientation.rotateAboutGlobalAxisDeg(0, 0, 1, -90);
	weapon_pistol->setLocalRot(pistolOrientation);

	// Dragunov orientation
	dragunovOrientation.identity();
	dragunovOrientation.rotateAboutGlobalAxisDeg(1, 0, 0, 90); // lean right left (- right + left)
	dragunovOrientation.rotateAboutGlobalAxisDeg(0, 1, 0, 0); // + up - down 
	dragunovOrientation.rotateAboutGlobalAxisDeg(0, 0, 1, 0);
	weapon_dragunov->setLocalRot(dragunovOrientation);

	// Rifle orientation
	rifleOrientation.identity();
	rifleOrientation.rotateAboutGlobalAxisDeg(1, 0, 0, 180);
	rifleOrientation.rotateAboutGlobalAxisDeg(0, 1, 0, 180); // + up - down
	rifleOrientation.rotateAboutGlobalAxisDeg(0, 0, 1, 0);
	weapon_rifle->setLocalRot(rifleOrientation);
}

//------------------------------------------------------------------------------

void updateWeaponPositionAndOrientation(cGenericHapticDevicePtr hapticDevice, cToolCursor* tool) {
	setInitialWeaponOrientations();
	// Get camera position and orientation
	cVector3d camPosition = camera->getLocalPos();

	// Define a fixed offset for the weapon in camera space
	cVector3d weaponOffset = cVector3d(-2.0, 0, 0);  // Adjust these values as needed

	// Calculate weapon position in world space
	cVector3d weaponPosition = camPosition + weaponOffset;

	tool->setLocalPos(weaponPosition);
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

void apply_pistol_force(void){
	float mf = 1.1;	// mass of firearm
	float vf = 3.978;	// velocity of firearm
	float mb = 0.015;	// mass of bullet 
	float barrel_length = 0.127;	// barrel length
	float tr = 0.003;	// recoil time

	float force = 0.15 * (vf / tr);

	cVector3d direction(1 + ((rand() % 20) - 10) / 100.0, ((rand() % 20) - 10) / 100.0, 0.3 + ((rand() % 20) - 10) / 100.0);
	direction.normalize();

	current_force = force*direction;

	float h_axis = 0.0678;
	current_torque = h_axis * current_force;
	float moment_of_inertia = (h_axis*h_axis)*mf;
	deviation_angle = (h_axis*mb*barrel_length) / moment_of_inertia;

	if (elapsed_time < 30){

		// apply the calculated force and torque
		hapticDevice->setForceAndTorque(current_force, current_torque*deviation_angle);

		//// visually render the recoil
		//cMatrix3d recoilRotation;
		//recoilRotation.identity();
		//recoilRotation.rotateAboutGlobalAxisDeg(1, 0, 0, 5 + (rand() % 5));

		//weapon_pistol->setLocalRot(weapon_pistol->getLocalRot() * recoilRotation);

	}
	else {
		hapticDevice->setForce(zero_vector);
	}
}

//------------------------------------------------------------------------------

void apply_rifle_force(void){
	float mf = 3.9;	// mass of firearm
	float vf = 2.2688;	// velocity of firearm
	float mb = 0.0079;	// mass of bullet 
	float barrel_length = 0.415;	// barrel length
	float tr = 0.06;	// recoil time

	float force = 0.15 * (vf / tr);

	cVector3d direction(1 + ((rand() % 20) - 10) / 100.0, ((rand() % 20) - 10) / 100.0, 0.3 + ((rand() % 20) - 10) / 100.0);
	// cVector3d direction(1, 0, 0);
	direction.normalize();

	current_force = force*direction * 100;

	float h_axis = 0.065;
	current_torque = h_axis * current_force;
	float moment_of_inertia = (h_axis*h_axis)*mf;
	deviation_angle = (h_axis*mb*barrel_length) / moment_of_inertia;


	if (elapsed_time < 60){

		// apply the calculated force and torque
		hapticDevice->setForceAndTorque(current_force, current_torque*deviation_angle);

	}
	else if (elapsed_time < 120) {
		hapticDevice->setForce(zero_vector);
	}
	else{
		time_start = currentTimeMillis();
		elapsed_time = 0;
	}
}

//------------------------------------------------------------------------------

void apply_sniper_force(void) {
	//std::lock_guard<std::mutex> deviceLock(deviceMutex);
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

	const int initial_spike_duration = 20;  // ms
	const int recoil_duration = 100;  // ms
	const int recovery_duration = 380;  // ms
	const int total_duration = initial_spike_duration + recoil_duration + recovery_duration;

	if (elapsed_time < total_duration) {
		sniperFiring = true;

		cVector3d current_force;
		cVector3d current_torque;

		if (elapsed_time < initial_spike_duration) {
			// Very high initial force
			float spike_factor = 1.0 - (float)elapsed_time / initial_spike_duration;
			current_force = initial_force * spike_factor;
			current_torque = initial_torque * spike_factor * deviation_angle;
		}
		else if (elapsed_time < initial_spike_duration + recoil_duration) {
			// Rapid decay after initial spike
			float decay_time = elapsed_time - initial_spike_duration;
			float decay_factor = exp(-10.0 * decay_time / recoil_duration);
			current_force = initial_force * decay_factor * 0.2; // 0.2 to reduce force after initial spike
			current_torque = initial_torque * decay_factor * deviation_angle * 0.2;
		}
		else {
			// Subtle recovery force in the opposite direction
			float recovery_progress = (float)(elapsed_time - initial_spike_duration - recoil_duration) / recovery_duration;
			float recovery_factor = exp(-3.0 * recovery_progress) * 0.05;  // 0.05 to make it more subtle
			current_force = -initial_force * recovery_factor;
			current_torque = -initial_torque * recovery_factor * deviation_angle;
		}

		// Apply the calculated force and torque
		hapticDevice->setForceAndTorque(current_force, current_torque);

		// Orientation Matrix :: Sniper
		cMatrix3d sniperRecoil;
		sniperRecoil.identity();

		float recoil_progress = (float)elapsed_time / total_duration;
		float recoil_angle_x = 90.0 * (1.0 - recoil_progress);
		float recoil_angle_y = 20.0 * (1.0 - recoil_progress);

		sniperRecoil.rotateAboutGlobalAxisDeg(1, 0, 0, 0); // lean right left (- right + left)
		sniperRecoil.rotateAboutGlobalAxisDeg(0, 1, 0, recoil_angle_y); // + up - down 
		sniperRecoil.rotateAboutGlobalAxisDeg(0, 0, 1, 0);
		weapon_dragunov->setLocalRot(sniperRecoil);
	}
	else {
		sniperFiring = false;
		hapticDevice->setForce(zero_vector);
	}
}
