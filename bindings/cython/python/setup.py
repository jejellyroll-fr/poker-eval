import sys
from setuptools import setup, Extension
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
        "pokereval",
        ["pokereval.pyx", "pokereval_wrapper.c"],
        include_dirs=[
            "../../../include"
        ],  # Ajustez selon le chemin de vos fichiers d'en-tête
        libraries=libraries,
        library_dirs=library_dirs,
        extra_compile_args=extra_compile_args,
        extra_link_args=extra_link_args,
    )
]

setup(
    name="pokereval",
    ext_modules=cythonize(extensions),
)
