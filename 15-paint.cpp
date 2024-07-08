//==============================================================================
/*
Software License Agreement (BSD License)
... [license text truncated for brevity] ...
*/
//==============================================================================

#include "chai3d.h"

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
cMultiMesh* weapon;
cBackground* background;
bool simulationRunning = false;
bool simulationFinished = true;
cFrequencyCounter frequencyCounter;
int screenW, screenH, windowW, windowH, windowPosX, windowPosY;
string resourceRoot;

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

	world = new cWorld();
	world->m_backgroundColor.setWhite();
	camera = new cCamera(world);
	world->addChild(camera);
	camera->set(cVector3d(0.8, 0.0, 0.0), cVector3d(0.0, 0.0, 0.0), cVector3d(0.0, 0.0, 1.0));
	camera->setClippingPlanes(0.01, 10.0);

	if (stereoMode == C_STEREO_DISABLED)
	{
		camera->setOrthographicView(1.3);
	}

	camera->setStereoMode(stereoMode);
	camera->setStereoEyeSeparation(0.01);
	camera->setStereoFocalLength(1.0);
	camera->setMirrorVertical(mirroredDisplay);
	camera->setUseMultipassTransparency(true);

	light = new cDirectionalLight(world);
	world->addChild(light);
	light->setEnabled(true);
	light->setDir(-1.0, 0.0, -0.4);

	// create a background object
	cBackground* background = new cBackground();
	// add background to back layer of camera
	camera->m_backLayer->addChild(background);
	// set aspect ration of background image a constant
	background->setFixedAspectRatio(true);
	// load an image file
	background->loadFromFile("6cdfaf7b7890b598475c13a447251f96.jpg");

	//--------------------------------------------------------------------------
	// HAPTIC DEVICES / TOOLS
	//--------------------------------------------------------------------------

	handler = new cHapticDeviceHandler();
	handler->getDevice(hapticDevice, 0);
	cHapticDeviceInfo hapticDeviceInfo = hapticDevice->getSpecifications();

	tool = new cToolCursor(world);
	world->addChild(tool);
	tool->setHapticDevice(hapticDevice);
	double toolRadius = 0.01;
	tool->setRadius(toolRadius);
	tool->setWorkspaceRadius(1.5);
	tool->setWaitForSmallForce(true);
	tool->start();

	//--------------------------------------------------------------------------
	// CREATE WEAPON PLACEHOLDER
	//--------------------------------------------------------------------------

	weapon = new cMultiMesh();
	world->addChild(weapon);

	bool fileload;
	fileload = weapon->loadFromFile(RESOURCE_PATH("../resources/drakefire_pistol_low.obj"));
	if (!fileload)
	{
		#if defined(_MSVC)
		fileload = weapon->loadFromFile("../../../bin/resources/drakefire_pistol_low.obj");
		#endif
	}
	if (!fileload)
	{
		cout << "Error - Model file failed to load correctly." << endl;
		close();
		return (-1);
	}

	// Apply JPG textures to the weapon
	cTexture2dPtr weaponTexture = cTexture2d::create();
	fileload = weaponTexture->loadFromFile(RESOURCE_PATH("../resources/textures/base_albedo.jpg"));
	if (!fileload)
	{
	#if defined(_MSVC)
		fileload = weaponTexture->loadFromFile("../../../bin/resources/textures/base_metallic.jpg");
	#endif
	}
	if (!fileload)
	{
		cout << "Error - Texture file failed to load correctly." << endl;
		close();
		return (-1);
	}

	// Apply the texture to all meshes of the weapon
	int numMeshes = weapon->getNumMeshes();
	for (int i = 0; i < numMeshes; i++)
	{
		cMesh* mesh = weapon->getMesh(i);
		mesh->setTexture(weaponTexture);
		mesh->setUseTexture(true);
	}

	weapon->setLocalPos(0.0, 0.2, -0.1); // Adjust these coordinates as necessary

	// Set the weapon orientation to point forwards
	cMatrix3d rotationMatrix;
	rotationMatrix.identity();
	rotationMatrix.rotateAboutGlobalAxisDeg(0, 1, 0, -90); // Adjust as needed
	rotationMatrix.rotateAboutGlobalAxisDeg(1, 0, 0, 90); // Adjust as needed
	rotationMatrix.rotateAboutGlobalAxisDeg(0, 0, 1, 15); // Adjust as needed
	weapon->setLocalRot(rotationMatrix);

	// disable culling so that faces are rendered on both sides
	weapon->setUseCulling(false);

	// scale model
	weapon->scale(0.3);

	// compute collision detection algorithm
	weapon->createAABBCollisionDetector(toolRadius);

	// Set material properties to ensure visibility
	weapon->setUseMaterial(true);
	cMaterial mat;
	weapon->setMaterial(mat);

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

	if (key == 'w'){
		double maxForce = hapticDevice->getSpecifications().m_maxLinearForce;
		cVector3d mForce(2*maxForce, -3*maxForce, 0.5*maxForce);
		cVector3d mTorque(maxForce, 0.0, maxForce);
		hapticDevice->setForceAndTorque(mForce * 100000000000, mTorque * 100000000000);
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

void updateHaptics(void)
{
	cPrecisionClock clock;
	clock.reset();
	simulationRunning = true;
	simulationFinished = false;

	while (simulationRunning)
	{
		clock.stop();
		double timeInterval = clock.getCurrentTimeSeconds();
		clock.reset();
		clock.start();

		world->computeGlobalPositions(true);
		tool->updateFromDevice();
		tool->computeInteractionForces();

		// Update weapon orientation based on the haptic device's orientation
		cMatrix3d toolRot = tool->getDeviceGlobalRot();

		tool->applyToDevice();
		frequencyCounter.signal(1);
	}

	simulationFinished = true;
}

void simulateRecoil()
{
	// Define the maximum force the device can apply safely
	double maxForce = hapticDevice->getSpecifications().m_maxLinearForce;
}