# Using Ogre in your App {#UsingOgreInYourApp}

@tableofcontents


# Overview {#UsingOgreInYourAppOverview}

We'll setup an application that uses OgreNext. For that, we'll use the Demo Framework that
comes with the samples as a starting point.

For OgreNext to work correctly, you'll need to register Hlms implementations.
We provide PBS (Physically Based Shading) and Unlit implementations for you to use out of the box.
These implementations will require you to include their data folders, it's not just header and libraries.

You will need to:
    * Include OgreMain/include
    * Include Components/Hlms/Common/include
    * Include Components/Hlms/Pbs/include
    * Include Components/Hlms/Unlit/include
    * Include build/Release/include (that's where OgreBuildSettings.h is). This path depends on the platform. On Windows (Visual Studio) it's build/include
    * Add the link path build/Release/lib/. This path depends on the platform. On Windows (Visual Studio) it's build/lib/Release
    * Link against OgreHlmsPbs.lib
    * Link against OgreHlmsUnlit.lib
    * Link against OgreMain.lib
    * Bundle the data files in Samples/Media/Hlms/Common with your application
    * Bundle the data files in Samples/Media/Hlms/Pbs with your application
    * Bundle the data files in Samples/Media/Hlms/Unlit with your application

**Notes:**
    * Do not put the Hlms data files into the same folder. Do not flatten the
      folders inside of each Hlms. In simple words, respect the directory structure.
    * For Debug builds, you'll obviously have to link against build/Debug/NameOfLibrary_d.
      Do **NOT** mix Debug and Release libraries. It can be done (depends on the platform) but it requires
      very advanced knowledge. Only if you know what you're doing.
    * On *nix OSes, replace OgreMain.lib with libOgreMain.so and so forth. You know the drill
    * If you've build the static version (i.e. OGRE_STATIC in CMake) you will
      also need to link and include every RenderSystem and plugin you want to use.
    * *Note for new users:* In Ogre, 'Components' are always linked statically to your project.
      'Plugins' instead can be loaded at runtime (unless Ogre was build with OGRE_STATIC_LIB)

**Optional**
    * Include Components/Overlay/include for 2D Overlays
    * Link against build/Release/lib/OgreOverlay for 2D Overlays
    * Bundle the data files in Samples/Media/2.0/scripts/materials/Common (highly recommended).
    * Bundle the data files in Samples/Media/packs/DebugPack.zip It contains a font file for
      text rendering, a Cube mesh, and a Sphere mesh. Very useful for debugging.




# Speeding things up {#SpeedingThingsUp}

Setting up a project to use Ogre can be time consuming.
We want you to kick directly into showing something on screen.
Fortunately, CMake can automate the job for us while also staying cross platform!

This script can be found in **Samples/2.0/Tutorials/EmptyProject**

**What this script does:**
It will setup a project with the following folder structure:
Folder                          | Description
--------------------------------|-------------------------------------------
build                           | Internally used by CMake. That's where the solution/project file for Visual Studio is stored (or makefiles for GNU Make, etc). Also likely object and PDB files from your compiler will end up there.
bin                             | That's where your binaries will be generated. Your binaries will actually be in bin/Release bin/Debug, etc.
bin/Data                        | That's where your assets should go. The script will automatically copy the Hlms data files to this location every time you run CMake's Generate.
CMake                           | Files used by the CMake script we're talking about.
CMake/Templates/Plugins.cfg.in  | Will be used to generate the real Plugins.cfg to know which plugins to load. The script will automatically generate the correct suffixes for the Debug builds, and skip RenderSystems that weren't built (e.g. D3D11 can never be used as a plugin in Linux). **You can edit it to add more plugins here**.
CMake/Templates/Resources.cfg.in| Will be used to generate the real Resource.cfg tells your app what locations to load for the assets. It needs to be automatically generated to correctly deal with iOS. **You can edit it to add your own folders here**.
Dependencies/Ogre               | The script expects Ogre to be there (can be tweaked). You can use symbolic links (`mklink /D` on Windows, `ln -s` in Linux). This is tweakable though.
include                         | Where your header files should be.
src                             | Where your cpp files will be located.
CMakeLists.txt                  | The script we're talking about.

The script accepts the following parameters:

Parameter           | Description
--------------------|-------------------------------------------
OGRE_SOURCE         | Where Ogre repository is located. By default it assumes it's at Dependencies/Ogre
OGRE_BINARIES       | Where Ogre was built (the folder generated by CMake). By default it assumes it's at Dependencies/Ogre/build
OGRE_DEPENDENCIES   | Path to Ogre's dependencies. By default it assumes it's Dependencies/Ogre/Dependencies or Dependencies/Ogre/iOSDependencies. Only used when Ogre was configured as static build.


## Small note about iOS {#SmallNoteAbout_iOS}

You will find the first line of CMakeLists.txt to be commented:
```CMake
#set( CMAKE_TOOLCHAIN_FILE CMake/iOS.cmake )
```
Uncomment it, and run the CMakeLists.txt script.

## Apple specific

To use EmptyProject in both iOS and macOS, OgreNext must have been compiled with CMake options:

	* `-DOGRE_BUILD_LIBS_AS_FRAMEWORKS=0`
	* `-DOGRE_STATIC=1`

## Creating your application with 'EmptyProject' script {#CreatingYourApplication}
    -# Copy Samples/2.0/Tutorials/EmptyProject to wherever you want. We'll refer to its location now as "EmptyProject"
    -# Copy Ogre repository (or use symbolic links) to EmptyProject/Dependencies/Ogre
    -# Run CMake on EmptyProject and build into EmptyProject/build (though you can choose a different path if you want).
    -# On Linux, CMake may complain that EmptyProject/Dependencies/Ogre/build/RelWithDebInfo was not found
       if you didn't build that version. In that case modify CMAKE_BUILD_TYPE and OGRE_BINARIES to point to the
       correct build type (e.g. Debug instead) and run Configure again.
    -# Compile the project.
    -# Run it. If necessary, configure the working directory to be EmptyProject/bin/Release
       (on Visual Studio, Alt+F7 -> Debugging -> Working Directory)

### A note about copied files from Samples/2.0/Common {#AnoteaboutcopiedfilesfromSamples_20_Common}

The script will copy all the files from Samples/2.0/Common so you already have a system in place
that takes care of window management and input and other misc stuff.

We believe the developer should be in charge of their entry points. Problem is,
not all platforms are the same:
    * On Linux you should use int main( int argc, const char *argv[] );
    * On Windows you should use WinMain (windows apps) or int main like in Linux (console)
    * On iOS you must declare int main, but Apple really wants you to use an AppDelegate.
    * On Android you the entry point is loaded then executed from your Java code.

Some frameworks address this problem by forcing you to derive
from some arbitrary class, or using macros.

**We don't force you to do anything**, but we provide utility
functions if you don't want to deal with issue.

While you could link against OgreSamplesCommon.lib (in fact that library does exist), the reality
is that your project will likely evolve and you may want to modify these files yourself, as they
control key aspects of the lifetime of your application.
Therefore we give you the freedom to do that.

You could link directly against OgreSamplesCommon.lib if you want, instead of copying the src files
from Samples/2.0/Common/src. Nothing is really stopping you from doing that.

## Supporting Multithreading loops from the get go {#SupportingMultithreadingLoopsFromTheGetGo}

Adding Multithreading after you've started your application can be much harder than doing it from the start.
We recommend you take a look at all the tutorial samples in this order:
    -# Tutorial01_Initialization
    -# Tutorial02_VariableFramerate
    -# Tutorial03_DeterministicLoop
    -# Tutorial04_InterpolationLoop
    -# Tutorial05_MultithreadingBasics
    -# Tutorial06_Multithreading
to familiarize yourself with Ogre and how to setup a basic main loop.
The last two tutorials show how to detach Logic and Physics simulation from Rendering, having
both communicating via messages passed at the end of the frame.

Once you understand how the multithreading approach works, replace `Demo::MainEntryPoints::mainAppSingleThreaded`
with `Demo::MainEntryPoints::mainAppMultiThreaded` in EmptyProjectGameState.cpp
and create valid pointers in MainEntryPoints::createSystems.
