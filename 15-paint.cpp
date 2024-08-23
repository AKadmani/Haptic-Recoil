#include "chai3d.h"
#include "system/CGlobals.h"
#include <chrono>
#include <random>

using namespace chai3d;
using namespace std;

#ifndef MACOSX
#include "GL/glut.h"
#else
#include "GLUT/glut.h"
#endif

//------------------------------------------------------------------------------
// GENERAL SETTINGS
//------------------------------------------------------------------------------
cStereoMode stereoMode = C_STEREO_DISABLED;
bool fullscreen = false;
bool mirroredDisplay = false;

//------------------------------------------------------------------------------
// DECLARED VARIABLES
//------------------------------------------------------------------------------
cWorld* world;
cCamera* camera;
cDirectionalLight* light;
cHapticDeviceHandler* handler;
cGenericHapticDevicePtr hapticDevice;
cToolCursor* tool;
cMultiMesh* weapon_pistol = nullptr;
cMultiMesh* weapon_dragunov = nullptr;
cMultiMesh* weapon_rifle = nullptr;
cBackground* background;
bool simulationRunning = false;
bool simulationFinished = true;
cFrequencyCounter frequencyCounter;
int screenW, screenH, windowW, windowH, windowPosX, windowPosY;
string resourceRoot;
cMatrix3d pistolOrientation;
cMatrix3d dragunovOrientation;
cMatrix3d rifleOrientation;
cLabel* weaponNameLabel;



//------------------------------------------------------------------------------
// DECLARED MACROS
//------------------------------------------------------------------------------
#define RESOURCE_PATH(p)    (char*)((resourceRoot+string(p)).c_str())

//------------------------------------------------------------------------------
// DECLARED FUNCTIONS
//------------------------------------------------------------------------------
void resizeWindow(int w, int h);
void keySelect(unsigned char key, int x, int y);
void updateGraphics(void);
void graphicsTimer(int data);
void close(void);
void updateHaptics(void);
void simulateRecoil(void);
void calculateAimRotation(const cMatrix3d& deviceRotation, cMatrix3d& aimRotation);
void updateWeaponOrientation(cGenericHapticDevicePtr device);
void fireWeapon(cMultiMesh* weapon, double fireRate);

__int64 time_start, time_end;

const int BURST_SIZE = 3;
int burstCount = 0;
bool isBurstFiring = false;
const double PISTOL_COOLDOWN = 0.9; // 0.5 seconds cooldown for pistol

// Function to apply texture to a weapon model
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

int main(int argc, char* argv[])
{
	cout << endl;
	cout << "-----------------------------------" << endl;
	cout << "CHAI3D" << endl;
	cout << "Shooting Simulator" << endl;
	cout << "-----------------------------------" << endl << endl << endl;
	cout << "Keyboard Options:" << endl << endl;
	cout << "[x] - Exit application" << endl;
	cout << endl << endl;

	resourceRoot = string(argv[0]).substr(0, string(argv[0]).find_last_of("/\\") + 1);

	//--------------------------------------------------------------------------
	// OPEN GL - WINDOW DISPLAY
	//--------------------------------------------------------------------------

	glutInit(&argc, argv);
	screenW = glutGet(GLUT_SCREEN_WIDTH);
	screenH = glutGet(GLUT_SCREEN_HEIGHT);
	windowW = 0.8 * screenH;
	windowH = 0.5 * screenH;
	windowPosY = (screenH - windowH) / 2;
	windowPosX = windowPosY;
	glutInitWindowPosition(windowPosX, windowPosY);
	glutInitWindowSize(windowW, windowH);

	if (stereoMode == C_STEREO_ACTIVE)
		glutInitDisplayMode(GLUT_RGB | GLUT_DEPTH | GLUT_DOUBLE | GLUT_STEREO);
	else
		glutInitDisplayMode(GLUT_RGB | GLUT_DEPTH | GLUT_DOUBLE);

	glutCreateWindow(argv[0]);

#ifdef GLEW_VERSION
	glewInit();
#endif

	glutDisplayFunc(updateGraphics);
	glutKeyboardFunc(keySelect);
	glutReshapeFunc(resizeWindow);
	glutSetWindowTitle("CHAI3D");

	if (fullscreen)
	{
		glutFullScreen();
	}

	//--------------------------------------------------------------------------
	// WORLD - CAMERA - LIGHTING
	//--------------------------------------------------------------------------

	// create a new world.
	world = new cWorld();

	// set the background color of the environment
	world->m_backgroundColor.setWhite();

	// create a camera and insert it into the virtual world
	camera = new cCamera(world);
	world->addChild(camera);

	// position and orient the camera
	camera->set(cVector3d(1.5, 0.0, 1.0),    // camera position (eye)
		cVector3d(0.0, 0.0, 0.0),    // lookat position (target)
		cVector3d(0.0, 0.0, 1.0));   // direction of the (up) vector

	// set the near and far clipping planes of the camera
	// anything in front or behind these clipping planes will not be rendered
	camera->setClippingPlanes(0.01, 100);

	// create a light source
	light = new cDirectionalLight(world);

	// add light to world
	world->addChild(light);

	// enable light source
	light->setEnabled(true);

	// define the direction of the light beam
	light->setDir(-1.0, -1.0, -1.0);

	// set lighting conditions
	light->m_ambient.set(0.4f, 0.4f, 0.4f);
	light->m_diffuse.set(0.8f, 0.8f, 0.8f);
	light->m_specular.set(1.0f, 1.0f, 1.0f);

	//--------------------------------------------------------------------------
	// HAPTIC DEVICES / TOOLS
	//--------------------------------------------------------------------------

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

	//--------------------------------------------------------------------------
	// CREATE OBJECTS
	//--------------------------------------------------------------------------

	// read the scale factor between the physical workspace of the haptic
	// device and the virtual workspace defined for the tool
	double workspaceScaleFactor = tool->getWorkspaceScaleFactor();

	// properties
	double maxStiffness = hapticDeviceInfo.m_maxLinearStiffness / workspaceScaleFactor;

	//--------------------------------------------------------------------------
	// CREATE WEAPON 
	//--------------------------------------------------------------------------

	// Initialize pistol model
	weapon_pistol = new cMultiMesh();
	bool fileload = weapon_pistol->loadFromFile(RESOURCE_PATH("../resources/1911.obj"));
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

	// Initialize Dragunov model
	weapon_dragunov = new cMultiMesh();
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

	// Initialize Rifle model
	weapon_rifle = new cMultiMesh();
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

	// Load and apply textures
	applyTextureToWeapon(weapon_pistol, "../resources/textures/pistol.png");
	applyTextureToWeapon(weapon_dragunov, "../resources/textures/Texture.png");
	applyTextureToWeapon(weapon_rifle, "../resources/textures/ak47.jpg");

	// Set initial weapon
	tool->m_image = weapon_pistol;

	weapon_pistol->scale(0.02);
	weapon_dragunov->scale(0.007);
	weapon_rifle->scale(0.3);

	// disable culling so that faces are rendered on both sides
	weapon_pistol->setUseCulling(false);
	weapon_dragunov->setUseCulling(false);
	weapon_rifle->setUseCulling(false);

	// compute collision detection algorithm
	weapon_pistol->createAABBCollisionDetector(toolRadius);
	weapon_dragunov->createAABBCollisionDetector(toolRadius);
	weapon_rifle->createAABBCollisionDetector(toolRadius);

	// define a default stiffness for the object
	weapon_pistol->setStiffness(0.1 * maxStiffness, true);
	weapon_dragunov->setStiffness(0.7 * maxStiffness, true);
	weapon_rifle->setStiffness(0.4 * maxStiffness, true);


	// use display list for faster rendering
	weapon_pistol->setUseDisplayList(true);
	weapon_dragunov->setUseDisplayList(true);
	weapon_rifle->setUseDisplayList(true);

	cVector3d devicePosition;
	hapticDevice->getPosition(devicePosition);


	weapon_pistol->setLocalPos(devicePosition);
	weapon_dragunov->setLocalPos(devicePosition);
	weapon_rifle->setLocalPos(devicePosition);
	weapon_rifle->translate(cVector3d(0.0, -1.0, 0.0));

	cMaterial mat;
	weapon_pistol->setMaterial(mat);
	weapon_dragunov->setMaterial(mat);
	weapon_rifle->setMaterial(mat);


	//--------------------------------------------------------------------------
	// WIDGETS
	//--------------------------------------------------------------------------

	// create a font
	cFont *font = NEW_CFONTCALIBRI32();

	// create a label to display the haptic rate of the simulation
	/*labelHapticRate = new cLabel(font);
	labelHapticRate->m_fontColor.setBlack();
	camera->m_frontLayer->addChild(labelHapticRate);*/


	// create a background object
	cBackground* background = new cBackground();
	// add background to back layer of camera
	camera->m_backLayer->addChild(background);
	// set aspect ration of background image a constant
	//background->setFixedAspectRatio(true);
	// load an image file
	background->loadFromFile("background.jpg");

	// Initialize and set up the weapon name label
	weaponNameLabel = new cLabel(font);
	weaponNameLabel->m_fontColor.setGreenDarkOlive();
	weaponNameLabel->setText("Current Weapon: M1911 PISTOL");  // Default weapon
	camera->m_frontLayer->addChild(weaponNameLabel);
	weaponNameLabel->setLocalPos(10, 10);  // Adjust position based on your UI layout


	//--------------------------------------------------------------------------
	// START SIMULATION
	//--------------------------------------------------------------------------

	simulationFinished = false;
	cThread* hapticsThread = new cThread();
	hapticsThread->start(updateHaptics, CTHREAD_PRIORITY_HAPTICS);
	atexit(close);
	glutTimerFunc(50, graphicsTimer, 0);
	glutMainLoop();

	return 0;
}

void resizeWindow(int w, int h)
{
	windowW = w;
	windowH = h;
}

void keySelect(unsigned char key, int x, int y)
{
	if ((key == 27) || (key == 'x'))
	{
		exit(0);
	}
}

void close(void)
{
	simulationRunning = false;
	while (!simulationFinished) { cSleepMs(100); }
	tool->stop();
}

void graphicsTimer(int data)
{
	if (simulationRunning)
	{
		glutPostRedisplay();
	}
	glutTimerFunc(50, graphicsTimer, 0);
}

void updateGraphics(void)
{
	world->updateShadowMaps(false, mirroredDisplay);

	camera->renderView(windowW, windowH);
	glutSwapBuffers();
	glFinish();
	GLenum err = glGetError();
	if (err != GL_NO_ERROR) cout << "Error: " << gluErrorString(err) << endl;
}

	// Global variables for weapon states
	bool isPistolLoaded = true;  // Start with pistol as default
	bool isDragunovLoaded = false;
	bool isRifleLoaded = false;
	// Add these to your global variables
	double lastFireTime = 0.0;
	const double FIRE_RATE_PISTOL = 0.03; // 2 shots per second
	const double FIRE_RATE_SNIPER = 1.0; // 1 shot per second
	const double FIRE_RATE_RIFLE = 0.1; // 10 shots per second (for automatic fire)

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

	// Function to update weapon orientation based on device rotation
	void updateWeaponOrientation(cGenericHapticDevicePtr device) {
		cMatrix3d deviceRotation;
		device->getRotation(deviceRotation);

		// Combine device rotation with initial weapon orientation
		if (isPistolLoaded) {
			weapon_pistol->setLocalRot(deviceRotation * pistolOrientation);
		}
		else if (isDragunovLoaded) {
			weapon_dragunov->setLocalRot(deviceRotation * dragunovOrientation);
		}
		else if (isRifleLoaded) {
			weapon_rifle->setLocalRot(deviceRotation * rifleOrientation);
		}
	}

	// Function to set initial weapon orientations
	void setInitialWeaponOrientations() {
		// Pistol orientation
		pistolOrientation.identity();
		pistolOrientation.rotateAboutGlobalAxisDeg(1, 0, 0, 115); // - up + down
		pistolOrientation.rotateAboutGlobalAxisDeg(0, 1, 0, 0);
		pistolOrientation.rotateAboutGlobalAxisDeg(0, 0, 1, -90);
		weapon_pistol->setLocalRot(pistolOrientation);

		// Dragunov orientation
		dragunovOrientation.identity();
		dragunovOrientation.rotateAboutGlobalAxisDeg(1, 0, 0, 90); // lean right left (- right + left)
		dragunovOrientation.rotateAboutGlobalAxisDeg(0, 1, 0, -30); // + up - down 
		dragunovOrientation.rotateAboutGlobalAxisDeg(0, 0, 1, 0);
		weapon_dragunov->setLocalRot(dragunovOrientation);

		// Rifle orientation
		rifleOrientation.identity();
		rifleOrientation.rotateAboutGlobalAxisDeg(1, 0, 0, 180);
		rifleOrientation.rotateAboutGlobalAxisDeg(0, 1, 0, 145);
		rifleOrientation.rotateAboutGlobalAxisDeg(0, 0, 1, 0);
		weapon_rifle->setLocalRot(rifleOrientation);
	}

	// Modify the RecoilState struct
	struct RecoilState {
		bool isRecoiling;
		double recoilTime;
		double recoilDuration;
		int shotsFired;
		cVector3d currentForce;
		cVector3d currentTorque;
		cMatrix3d currentRotation;
		bool canFire; // New field to handle cooldown
	};	
	// Global RecoilState for each weapon
	RecoilState pistolRecoilState;
	RecoilState rifleRecoilState;
	RecoilState sniperRecoilState;

	// Modify the applyForce function to handle different weapon types
	void applyForce(cVector3d direction, float vf, float tr, RecoilState& recoilState) {
		float force = 0.15 * (vf / tr);
		cVector3d force_with_direction = force * direction;
		force_with_direction.mul(100.0);
		hapticDevice->setForce(force_with_direction);

		recoilState.isRecoiling = true;
		recoilState.recoilTime = 0;
		recoilState.currentForce = force_with_direction;
	}
	void fireWeapon(cMultiMesh* weapon, double fireRate, RecoilState& recoilState) {
		if (!recoilState.canFire) return;

		cMatrix3d recoilRotation;
		recoilRotation.identity();
		recoilRotation.rotateAboutGlobalAxisDeg(1, 0, 0, 5 + (rand() % 5));

		weapon->setLocalRot(weapon->getLocalRot() * recoilRotation);

		float vf = 6.153;
		float tr = 0.003;
		cVector3d direction(1 + ((rand() % 20) - 10) / 100.0, ((rand() % 20) - 10) / 100.0, 0.3 + ((rand() % 20) - 10) / 100.0);
		direction.normalize();

		// Increase force for pistol and sniper
		if (weapon == weapon_pistol) {
			vf *= 1.5;  // Increase pistol force by 50%
		}
		else if (weapon == weapon_dragunov) {
			vf *= 3.0;  // Triple the force for sniper
			// tr *= 2.0;  // Increase duration of force for sniper
		}

		applyForce(direction, vf, tr, recoilState);

		recoilState.recoilDuration = fireRate * 2;
		recoilState.shotsFired++;
		recoilState.currentRotation = recoilRotation;
		recoilState.canFire = false;
	}
	// Add this function to update recoil state
	void updateRecoilState(RecoilState& recoilState, double deltaTime, cMultiMesh* weapon) {
		if (!recoilState.isRecoiling) return;

		recoilState.recoilTime += deltaTime;

		float recoilProgress = recoilState.recoilTime / recoilState.recoilDuration;
		float forceMagnitude = recoilState.currentForce.length() * (1.0 - recoilProgress) * sin(recoilProgress * M_PI);

		cVector3d currentForce = recoilState.currentForce;
		currentForce.normalize();
		currentForce.mul(forceMagnitude);

		hapticDevice->setForce(currentForce);

		cMatrix3d rotationDelta;
		rotationDelta.setAxisAngleRotationDeg(cVector3d(1, 0, 0), forceMagnitude * 0.5);
		recoilState.currentRotation = recoilState.currentRotation * rotationDelta;

		weapon->setLocalRot(weapon->getLocalRot() * recoilState.currentRotation);

		if (recoilState.recoilTime >= recoilState.recoilDuration) {
			recoilState.isRecoiling = false;
			recoilState.currentForce.zero();
			recoilState.currentTorque.zero();
		}
	}
	// Modify the updateHaptics function
	void updateHaptics(void)
	{
		setInitialWeaponOrientations();

		auto lastFireTime = std::chrono::high_resolution_clock::now();

		simulationRunning = true;
		simulationFinished = false;

		pistolRecoilState.canFire = true;
		rifleRecoilState.canFire = true;
		sniperRecoilState.canFire = true;

		while (simulationRunning)
		{
			auto currentTime = std::chrono::high_resolution_clock::now();
			double deltaTime = std::chrono::duration<double>(currentTime - lastFireTime).count();

			frequencyCounter.signal(1);

			world->computeGlobalPositions(true);
			tool->updateFromDevice();
			updateWeaponOrientation(hapticDevice);

			bool button0, button1, button2, button3;
			hapticDevice->getUserSwitch(0, button0);
			hapticDevice->getUserSwitch(1, button1);
			hapticDevice->getUserSwitch(2, button2);
			hapticDevice->getUserSwitch(3, button3);

			// Button 0: Fire weapon
			if (button0) {
				if (isPistolLoaded && pistolRecoilState.canFire) {
					fireWeapon(weapon_pistol, FIRE_RATE_PISTOL, pistolRecoilState);
					lastFireTime = currentTime;
				}
				else if (isDragunovLoaded && sniperRecoilState.canFire) {
					fireWeapon(weapon_dragunov, FIRE_RATE_SNIPER, sniperRecoilState);
					lastFireTime = currentTime;
				}
				else if (isRifleLoaded && deltaTime >= FIRE_RATE_RIFLE) {
					fireWeapon(weapon_rifle, FIRE_RATE_RIFLE, rifleRecoilState);
					lastFireTime = currentTime;
				}
			}

			// Update recoil states
			if (isPistolLoaded) {
				updateRecoilState(pistolRecoilState, deltaTime, weapon_pistol);
				if (deltaTime >= PISTOL_COOLDOWN) {
					pistolRecoilState.canFire = true;
				}
			}
			else if (isDragunovLoaded) {
				updateRecoilState(sniperRecoilState, deltaTime, weapon_dragunov);
				if (deltaTime >= FIRE_RATE_SNIPER) {
					sniperRecoilState.canFire = true;
				}
			}
			else if (isRifleLoaded) {
				updateRecoilState(rifleRecoilState, deltaTime, weapon_rifle);
				rifleRecoilState.canFire = true; // Rifle can always fire due to automatic nature
			}

			// Weapon switching logic (unchanged)
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

			tool->computeInteractionForces();
			tool->applyToDevice();
		}

		simulationFinished = true;
	}