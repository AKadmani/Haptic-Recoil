# OASIS - Shooting Simulator

OASIS is an immersive shooting simulator built using the CHAI3D framework and designed specifically for the Novint Falcon haptic device. This project aims to provide a realistic and interactive shooting experience with 3 degrees of freedom (3DOF) haptic feedback.

## Features

- Multiple weapon types: M1911 Pistol, AK47 Rifle, and Dragunov Sniper Rifle
- Realistic weapon handling and recoil simulation using the Novint Falcon's 3DOF haptic feedback
- Dynamic target system
- Time trial mode for skill assessment
- 3D environment with obstacle blocks
- Customizable lighting effects
- Designed for the Novint Falcon haptic device

## Requirements

- CHAI3D library
- OpenGL and GLUT
- C++ compiler with C++11 support
- Novint Falcon haptic device

## Building and Running

1. Ensure you have the CHAI3D library installed and properly configured in your development environment.
2. Make sure your Novint Falcon device is properly connected and drivers are installed.
3. Clone this repository:
   ```
   git clone https://github.com/AKadmani/Haptic-Recoil.git
   ```
4. Navigate to the project directory and build the project using your preferred C++ build system (e.g., CMake, Make).
5. Run the compiled executable.

## Controls

- Novint Falcon movement: Aim and control weapon position
- Falcon primary button: Fire weapon
- Falcon secondary buttons: Switch between weapons
- Keyboard controls:
  - `W`, `A`, `S`, `D`: Move the camera
  - `Q`, `E`: Rotate the weapon
  - `T`: Start time trial mode
  - `X`: Exit the application

## Novint Falcon Integration

The OASIS Shooting Simulator is specifically designed to work with the Novint Falcon haptic device. It utilizes the device's 3 degrees of freedom to provide realistic weapon handling and haptic feedback:

- The 3D position of the Falcon's end effector controls the aim and position of the weapon in the virtual environment.
- The primary button on the Falcon grip is used to fire the currently selected weapon.
- The additional buttons on the Falcon grip are used to switch between different weapon types.
- Haptic feedback is provided through the Falcon to simulate recoil and other weapon characteristics.

## Contributing

Contributions to OASIS are welcome! Please feel free to submit pull requests, create issues or spread the word.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Acknowledgements

- CHAI3D framework developers
- Novint Technologies for the Falcon haptic device
- Technical University of Munich
- [Recoil energy calculator](shooterscalculator.com)
- [Haptically enabled simulation system for firearm shooting training](https://www.researchgate.net/publication/325792582_Haptically_enabled_simulation_system_for_firearm_shooting_training)

## Disclaimer

This simulator is for educational and entertainment purposes only. It is not intended for actual firearms training.
