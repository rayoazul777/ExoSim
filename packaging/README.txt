ExoSim - orbital simulation and visualization
==============================================

QUICK START
  1. Extract this whole folder (don't run ExoSim.exe from inside the zip).
  2. Double-click ExoSim.exe.
     A console window opens alongside the app; it shows load/status messages.

  Windows may say "Windows protected your PC" because the exe is not
  code-signed. Click "More info" -> "Run anyway".

REQUIREMENTS
  - Windows 10 or 11, 64-bit
  - A GPU/driver supporting OpenGL 3.3 (almost any machine from the last
    decade). Some remote-desktop and virtual-machine sessions do not
    provide OpenGL 3.3; the app will report an error on startup.
  - No other installs needed: all libraries and the C++ runtime are built in.

CHECK THE INSTALL WITHOUT A GPU
  ExoSim.exe --selftest    Verifies shaders and data load; prints PASSED/FAILED
  ExoSim.exe --version     Prints the version

CONTROLS
  Left-drag: orbit camera.  Scroll: zoom.
  Pick a star system from the list; "Inspect Planet" / "Inspect <moon>" for detail.
  Sol (our Solar System) has a Mission Planner window for launch windows.

DATA
  data/ holds a snapshot of real catalog data fetched when this build was
  made (see data/FETCHED_AT.txt). To refresh it, run in this folder:
      FetchData.exe --out-dir data
  FetchData.exe needs an internet connection.

WHAT'S REAL AND WHAT ISN'T
  Values in the app are labeled measured, derived, estimated or
  illustrative. Exoplanet surfaces, speculative moons and exoplanet
  orbital phases are illustrative. Mission plans are idealized Hohmann
  transfers (no gravity assists). See DATA_SOURCES.md for sources.

LICENSE
  ExoSim's code is released under the MIT License (see LICENSE).
  The bundled catalog data in data/ is NOT covered by that license; it
  remains under its providers' terms (see DATA_SOURCES.md).

ACKNOWLEDGMENTS
  This research has made use of the NASA Exoplanet Archive, which is
  operated by the California Institute of Technology, under contract with
  the National Aeronautics and Space Administration under the Exoplanet
  Exploration Program.
  This research has made use of the VizieR catalogue access tool, CDS,
  Strasbourg, France (DOI: 10.26093/cds/vizier).
  Hipparcos catalogue: ESA (1997), ESA SP-1200.
  Washington Double Star Catalog: Mason et al. (2001), AJ 122, 3466.

This is not flight software. See CHANGELOG.md for version history.
