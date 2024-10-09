from setuptools import setup, Extension
from Cython.Build import cythonize

extensions = [
    Extension(
        "pokereval",
        ["pokereval.pyx", "pokereval_wrapper.c"],
        include_dirs=["../../../include"],  # Adjust to the path of your include files
        libraries=["poker_lib_static"],  # Assuming this is the library name
        library_dirs=[
            "../../../build/Debug",
            "../../../lib",
        ],  # Adjust to the path of your compiled lib
        extra_compile_args=["/Zi"],  # Facultatif : pour le débogage sous Windows
        extra_link_args=[
            "/NODEFAULTLIB:MSVCRTD",
            "/DEBUG",
        ],  # Résoudre le conflit de bibliothèques par défaut
    )
]

setup(
    name="pokereval",
    ext_modules=cythonize(extensions),
)
