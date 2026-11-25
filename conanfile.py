from conan import ConanFile
from conan.tools.cmake import cmake_layout
from conan.tools.gnu import PkgConfigDeps


class PrismConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"

    def requirements(self):
        self.requires("sdl/3.4.16")

    def configure(self):
        if self.settings.os == "Linux":
            # libudev/system needs the libudev-dev apt package; SDL falls back
            # to inotify for gamepad hotplug without it.
            self.options["sdl"].libudev = False

    def generate(self):
        if self.settings.os == "Linux":
            PkgConfigDeps(self).generate()  # for the Makefile

    def layout(self):
        cmake_layout(self)
