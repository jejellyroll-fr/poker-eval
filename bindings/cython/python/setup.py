import sys
from setuptools import setup, Extension, find_packages
from Cython.Build import cythonize

# Configuration spécifique par plateforme
if sys.platform == "win32":
    extra_compile_args = ["/Zi"]
    extra_link_args = ["/NODEFAULTLIB:MSVCRTD", "/DEBUG"]
    libraries = ["poker_lib_static"]
    library_dirs = ["../../../build/Debug", "../../../lib"]
elif sys.platform == "darwin":  # macOS
    extra_compile_args = ["-g"]  # Option de débogage pour macOS
    extra_link_args = ["-g"]
    libraries = ["poker_lib_static"]
    library_dirs = ["../../../build", "../../../lib"]
elif sys.platform.startswith("linux"):
    extra_compile_args = ["-g"]
    extra_link_args = ["-g"]
    libraries = ["poker_lib_static"]
    library_dirs = ["../../../build", "../../../lib"]

extensions = [
    Extension(
        "pokereval.pokereval",
        ["pokereval/pokereval.pyx", "pokereval/pokereval_wrapper.c"],
        include_dirs=[
            "../../../include"
        ],
        libraries=libraries,
        library_dirs=library_dirs,
        extra_compile_args=extra_compile_args,
        extra_link_args=extra_link_args,
    )
]

setup(
    name="pokereval",
    version="0.1.0",
    packages=find_packages(),
    package_dir={"": "."},  # Ajout pour préciser où chercher le package
    ext_modules=cythonize(extensions),
    zip_safe=False,
)
