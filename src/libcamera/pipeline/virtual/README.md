# Virtual Pipeline Handler

Virtual pipeline handler emulates fake external camera(s) on ChromeOS for testing.

## Parse config file and register cameras

- The config file is located at `src/libcamera/pipeline/virtual/data/virtual.yaml`

### Config File Format
The config file contains the information about cameras' properties to register.
The config file should be a yaml file with dictionary of the cameraIds
associated with their properties as top level. The default value will be applied when any property is empty.

Each camera block is a dictionary, containing the following keys:
- `supported_formats` (list of `VirtualCameraData::Resolution`, optional) : List of supported resolution and frame rates of the emulated camera
    - `width` (`unsigned int`, default=1920): Width of the window resolution. This needs to be even.
    - `height` (`unsigned int`, default=1080): Height of the window resolution.
    - `frame_rates` (list of `int`, default=`[30,60]` ): Range of the frame rate. The list has to be two values of the lower bound and the upper bound of the frame rate.
- `test_pattern` (`string`, default="bars"): Which test pattern to use as frames. The options are "bars", "lines".
- `location` (`string`, default="front"): The location of the camera. Support "front" and "back". This is displayed in qcam camera selection window but this does not change the output.
- `model` (`string`, default="Unknown"): The model name of the camera. This is displayed in qcam camera selection window but this does not change the output.

A sample config file:
```
---
"Virtual0":
  supported_formats:
  - width: 1920
    height: 1080
    frame_rates:
    - 30
    - 60
  - width: 1680
    height: 1050
    frame_rates:
    - 70
    - 80
  test_pattern: "bars"
  location: "front"
  model: "Virtual Video Device"
"Virtual1":
  supported_formats:
  - width: 800
  test_pattern: "lines"
  location: "back"
  model: "Virtual Video Device1"
"Virtual2":
```

### Implementation

`Parser` class provides methods to parse the config file to register cameras
in Virtual Pipeline Handler. `parseConfigFile()` is exposed to use in
Virtual Pipeline Handler.

This is the procedure of the Parser class:
1. `parseConfigFile()` parses the config file to `YamlObject` using `YamlParser::parse()`.
    - Parse the top level of config file which are the camera ids and look into each camera properties.
2. For each camera, `parseCameraConfigData()` returns a camera with the configuration.
    - The methods in the next step fill the data with the pointer to the Camera object.
    - If the config file contains invalid configuration, this method returns nullptr. The camera will be skipped.
3. Parse each property and register the data.
    - `parseSupportedFormats()`: Parses `supported_formats` in the config, which contains resolutions and frame rates.
    - `parseTestPattern()`: Parses `test_pattern` in the config.
    - `parseLocation()`: Parses `location` in the config.
    - `parseModel()`: Parses `model` in the config.
4. Back to `parseConfigFile()` and append the camera configuration.
5. Returns a list of camera configurations.
