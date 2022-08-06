from setuptools import setup, Extension
import os

setup(
    name="PyCriCodecs",
    version="0.0.1",
    description="Python frontend with a C++ backend of managing Criware files of all kind.",
    packages=["PyCriCodecs"],
    ext_modules=[Extension('CriCodecs', ["CriCodecs\\CriCodecs.cpp"], include_dirs=[os.path.realpath("CriCodecs")])]
)