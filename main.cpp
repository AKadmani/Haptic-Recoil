#include "chai3d.h"
//------------------------------------------------------------------------------
using namespace chai3d;
using namespace std;
//------------------------------------------------------------------------------
#ifndef MACOSX
#include "GL/glut.h"
#else
#include "GLUT/glut.h"
#endif

//------------------------------------------------------------------------------
// GENERAL SETTINGS
//------------------------------------------------------------------------------

// stereo Mode
/*
C_STEREO_DISABLED:            Stereo is disabled
C_STEREO_ACTIVE:              Active stereo for OpenGL NVDIA QUADRO cards
C_STEREO_PASSIVE_LEFT_RIGHT:  Passive stereo where L/R images are rendered next to each other
C_STEREO_PASSIVE_TOP_BOTTOM:  Passive stereo where L/R images are rendered above each other
*/
cStereoMode stereoMode = C_STEREO_DISABLED;

// fullscreen mode
bool fullscreen = false;

// mirrored display
bool mirroredDisplay = false;

//------------------------------------------------------------------------------
// DECLARED VARIABLES
//------------------------------------------------------------------------------

// a world that contains all objects of the virtual environment
cWorld* world;

// a camera to render the world in the window display
cCamera* camera;

// a light source to illuminate the objects in the world
cDirectionalLight *light;

// a haptic device handler
cHapticDeviceHandler* handler;

// a pointer to the current haptic device
cGenericHapticDevicePtr hapticDevice;

cToolCursor* tool;

// a label to display the rate [Hz] at which the simulation is running
cLabel* labelHapticRate;

// flag to indicate if the haptic simulation currently running
bool simulationRunning = false;

// flag to indicate if the haptic simulation has terminated
bool simulationFinished = true;

// frequency counter to measure the simulation haptic rate
cFrequencyCounter frequencyCounter;

// information about computer screen and GLUT display window
int screenW;
int screenH;
int windowW;
int windowH;
int windowPosX;
int windowPosY;

// multi-mesh models for the different weapons
cMultiMesh* weapon_pistol = nullptr;
cMultiMesh* weapon_dragunov = nullptr;
cMultiMesh* weapon_rifle = nullptr;

// orientation matrices for the different weapons
cMatrix3d pistolOrientation;
cMatrix3d dragunovOrientation;
cMatrix3d rifleOrientation;

// Global variables for weapon states
bool isPistolLoaded = true;  // Start with pistol as default
bool isDragunovLoaded = false;
bool isRifleLoaded = false;

// a label to display the current weapon name
cLabel* weaponNameLabel;

// a background to set the scene
cBackground* background;

// root path for the resource files
string resourceRoot;

// for realistic haptic rendering
bool is_pressed;

// timing requirements for recoil
__int64 time_start;
__int64 time_end;
int elapsed_time;

// global variables for the haptic force and torque
cVector3d current_force;
cVector3d current_torque;
float deviation_angle;

// global zero vector for ease of coding
cVector3d zero_vector(0, 0, 0);
//------------------------------------------------------------------------------
// DECLARED MACROS
//------------------------------------------------------------------------------

#define RESOURCE_PATH(p)    (char*)((resourceRoot+string(p)).c_str())

//------------------------------------------------------------------------------
// DECLARED FUNCTIONS
//------------------------------------------------------------------------------
// callback when the window display is resized
void resizeWindow(int w, int h);

// callback when a key is pressed
void keySelect(unsigned char key, int x, int y);

// callback to render graphic scene
void updateGraphics(void);

// callback of GLUT timer
void graphicsTimer(int data);

// function that closes the application
void close(void);

// main haptics simulation loop
void updateHaptics(void);

// function to calculate current time in milliseconds
__int64 currentTimeMillis();

// Function to apply texture to a weapon model
void applyTextureToWeapon(cMultiMesh* weapon, const std::string& texturePath);

// set the initial weapon orientations according to the 3D models
void setInitialWeaponOrientations();

// update the orientation of the weapon
void updateWeaponOrientation(cGenericHapticDevicePtr device);

// update the label with the current weapon
void updateWeaponLabel(void);

// main functions for visual & haptic rendering of the weapons
void apply_pistol_force(void);
void apply_sniper_force(void);
void apply_rifle_force(void);

//------------------------------------------------------------------------------

int main(int argc, char* argv[])
{

	//--------------------------------------------------------------------------
	// INITIALIZATION
	//--------------------------------------------------------------------------

	cout << endl;
	cout << "-----------------------------------" << endl;
	cout << "CHAI3D" << endl;
	cout << "OASIS - Shooting Simulator" << endl;
	cout << "-----------------------------------" << endl << endl << endl;
	cout << "Keyboard Options:" << endl << endl;
	cout << "[x] - Exit application" << endl;
	cout << endl << endl;

	//--------------------------------------------------------------------------
	// OPENGL - WINDOW DISPLAY
	//--------------------------------------------------------------------------

	// initialize GLUT
	glutInit(&argc, argv);

	// retrieve  resolution of computer display and position window accordingly
	screenW = glutGet(GLUT_SCREEN_WIDTH);
	screenH = glutGet(GLUT_SCREEN_HEIGHT);
	windowW = (int)(0.8 * screenH);
	windowH = (int)(0.5 * screenH);
	windowPosY = (screenH - windowH) / 2;
	windowPosX = windowPosY;

	// initialize the OpenGL GLUT window
	glutInitWindowPosition(windowPosX, windowPosY);
	glutInitWindowSize(windowW, windowH);

	if (stereoMode == C_STEREO_ACTIVE)
		glutInitDisplayMode(GLUT_RGB | GLUT_DEPTH | GLUT_DOUBLE | GLUT_STEREO);
	else
		glutInitDisplayMode(GLUT_RGB | GLUT_DEPTH | GLUT_DOUBLE);

	// create display context and initialize GLEW library
	glutCreateWindow(argv[0]);

#ifdef GLEW_VERSION
	// initialize GLEW
	glewInit();
#endif

	// setup GLUT options
	glutDisplayFunc(updateGraphics);
	glutKeyboardFunc(keySelect);
	glutReshapeFunc(resizeWindow);
	glutSetWindowTitle("CHAI3D");

	// set fullscreen mode
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

	// create a haptic device handler
	handler = new cHapticDeviceHandler();

	// get a handle to the first haptic device
	handler->getDevice(hapticDevice, 0);

	// get specifications of the haptic device
	cHapticDeviceInfo hapticDeviceInfo = hapticDevice->getSpecifications();

	// add a cursor tool 
	tool = new cToolCursor(world);

	// add tool to world
	world->addChild(tool);

	// connect the haptic device to the tool
	tool->setHapticDevice(hapticDevice);

	// set the tool radius
	double toolRadius = 0.001;
	tool->setRadius(toolRadius);

	// initialize the tool settings
	tool->setWorkspaceRadius(1.0);
	tool->setWaitForSmallForce(true);
	tool->start();
	tool->setUseTransparency(true);

	// read the scale factor between the physical workspace of the haptic device and the virtual workspace defined for the tool
	double workspaceScaleFactor = tool->getWorkspaceScaleFactor();

	// properties
	double maxStiffness = hapticDeviceInfo.m_maxLinearStiffness / workspaceScaleFactor;

	//--------------------------------------------------------------------------
	// WIDGETS
	//--------------------------------------------------------------------------

	// create a font
	cFont *font = NEW_CFONTCALIBRI32();

	// create a label to display the haptic rate of the simulation
	labelHapticRate = new cLabel(font);
	labelHapticRate->m_fontColor.setBlack();
	camera->m_frontLayer->addChild(labelHapticRate);


	// create a background object
	cBackground* background = new cBackground();

	// add background to back layer of camera
	camera->m_backLayer->addChild(background);

	// load an image file
	background->loadFromFile("background.jpg");

	// Initialize and set up the weapon name label
	weaponNameLabel = new cLabel(font);
	weaponNameLabel->m_fontColor.setGreenDarkOlive();
	weaponNameLabel->setText("Current Weapon: M1911 PISTOL");  // Default weapon
	camera->m_frontLayer->addChild(weaponNameLabel);
	weaponNameLabel->setLocalPos(10, 10);  // Adjust position based on your UI layout


	//--------------------------------------------------------------------------
	// CREATE WEAPONS
	//--------------------------------------------------------------------------

	// Initialize M1911 model (Pistol)
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
	
	// Initialize Dragunov model (Sniper)
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

	// Initialize AK47 model (Assault  Rifle)
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

	// Set scales depending on the 3D models
	weapon_pistol->scale(0.02);
	weapon_dragunov->scale(0.007);
	weapon_rifle->scale(0.3);

	// Load and apply textures
	applyTextureToWeapon(weapon_pistol, "../resources/textures/pistol.png");
	applyTextureToWeapon(weapon_dragunov, "../resources/textures/Texture.png");
	applyTextureToWeapon(weapon_rifle, "../resources/textures/ak47.jpg");

	// Set initial weapon (default: pistol)
	tool->m_image = weapon_pistol;

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

	// set the spawning positions of the weapons 
	cVector3d devicePosition;
	hapticDevice->getPosition(devicePosition);

	weapon_pistol->setLocalPos(devicePosition);
	weapon_dragunov->setLocalPos(devicePosition);
	weapon_rifle->setLocalPos(devicePosition);
	weapon_rifle->translate(cVector3d(0.0, -1.0, 0.0));

	// set materials for the weapons
	cMaterial mat;
	weapon_pistol->setMaterial(mat);
	weapon_dragunov->setMaterial(mat);
	weapon_rifle->setMaterial(mat);

	//--------------------------------------------------------------------------
	// START SIMULATION
	//--------------------------------------------------------------------------

	simulationFinished = false;

	// create a thread which starts the main haptics rendering loop
	cThread* hapticsThread = new cThread();
	hapticsThread->start(updateHaptics, CTHREAD_PRIORITY_HAPTICS);

	// setup callback when application exits
	atexit(close);

	// start the main graphics rendering loop
	glutTimerFunc(50, graphicsTimer, 0);
	glutMainLoop();

	// exit
	return (0);
}

//------------------------------------------------------------------------------

void resizeWindow(int w, int h)
{
	windowW = w;
	windowH = h;
}

//------------------------------------------------------------------------------

void keySelect(unsigned char key, int x, int y)
{
	if ((key == 27) || (key == 'x'))
	{
		exit(0);
	}
}

//------------------------------------------------------------------------------

void close(void)
{
	simulationRunning = false;
	while (!simulationFinished) { cSleepMs(100); }
	tool->stop();
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

//------------------------------------------------------------------------------

void updateHaptics(void)
{
	setInitialWeaponOrientations();

	simulationRunning = true;
	simulationFinished = false;
	
	while (simulationRunning)
	{

		world->computeGlobalPositions(true);
		tool->updateFromDevice();
		updateWeaponOrientation(hapticDevice);

		// check if the elapsed time needs to be calculated
		if (is_pressed){
			elapsed_time = currentTimeMillis() - time_start;
		}
		else {
			elapsed_time = 0;
		}

		// read user-switch statuses
		bool button0, button1, button2, button3;
		button0 = false;
		button1 = false;
		button2 = false;
		button3 = false;
		hapticDevice->getUserSwitch(0, button0);
		hapticDevice->getUserSwitch(1, button1);
		hapticDevice->getUserSwitch(2, button2);
		hapticDevice->getUserSwitch(3, button3);

		// main simulator logic
		if (!is_pressed  && button0){
			is_pressed = true;
			time_start = currentTimeMillis();
		}

		if (is_pressed && button0){
			if (isPistolLoaded){
				apply_pistol_force();
			}
			else if (isRifleLoaded){
				apply_rifle_force();
			}
			else if (isDragunovLoaded){
				apply_sniper_force();
			}
		}

		if (!(is_pressed && button0)){
			hapticDevice->setForce(zero_vector);
		}

		if (is_pressed && !button0){
			hapticDevice->setForce(zero_vector);
			is_pressed = false;
		}

		// Weapon switching logic

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
		//tool->applyToDevice();
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

void setInitialWeaponOrientations(void) {
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

//------------------------------------------------------------------------------

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

	//cVector3d direction(1 + ((rand() % 20) - 10) / 100.0, ((rand() % 20) - 10) / 100.0, 0.3 + ((rand() % 20) - 10) / 100.0);
	cVector3d direction(1, 0, 0);
	direction.normalize();

	current_force = force*direction * 100;

	float h_axis = 0.065;
	current_torque = h_axis * current_force;
	float moment_of_inertia = (h_axis*h_axis)*mf;
	deviation_angle = (h_axis*mb*barrel_length) / moment_of_inertia;

	
	if (elapsed_time < 60){

		// apply the calculated force and torque
		hapticDevice->setForceAndTorque(current_force, current_torque*deviation_angle);

		//// visually render the recoil
		//cMatrix3d recoilRotation;
		//recoilRotation.identity();
		//recoilRotation.rotateAboutGlobalAxisDeg(1, 0, 0, 5 + (rand() % 5));

		//weapon_rifle->setLocalRot(weapon_rifle->getLocalRot() * recoilRotation);
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

void apply_sniper_force(void){
	float mf = 4.3;	// mass of firearm
	float vf = 3.265;	// velocity of firearm
	float mb = 0.0113;	// mass of bullet 
	float barrel_length = 0.62;	// barrel length
	float tr = 0.01;	// recoil time

	float force = 0.15 * (vf / tr);

	cVector3d direction(1 + ((rand() % 20) - 10) / 100.0, ((rand() % 20) - 10) / 100.0, 0.3 + ((rand() % 20) - 10) / 100.0);
	direction.normalize();

	current_force = force*direction;

	float h_axis = 0.045;
	current_torque = h_axis * current_force;
	float moment_of_inertia = (h_axis*h_axis)*mf;
	deviation_angle = (h_axis*mb*barrel_length) / moment_of_inertia;

	if (elapsed_time < 100){

		// apply the calculated force and torque
		hapticDevice->setForceAndTorque(current_force, current_torque*deviation_angle);

		//// visually render the recoil
		//cMatrix3d recoilRotation;
		//recoilRotation.identity();
		//recoilRotation.rotateAboutGlobalAxisDeg(1, 0, 0, 5 + (rand() % 5));

		//weapon_dragunov->setLocalRot(weapon_dragunov->getLocalRot() * recoilRotation);
	}
	else if (elapsed_time < 500) {
		hapticDevice->setForce(zero_vector);
	}
	else {
		hapticDevice->setForce(zero_vector);
	}

}

//------------------------------------------------------------------------------
