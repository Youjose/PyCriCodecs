from setuptools import setup, Extension
import os

setup(
    name="PyCriCodecs",
    version="0.4.6",
    description="Python frontend with a C++ backend of managing Criware files of all kinds.",
    packages=["PyCriCodecs"],
    ext_modules=[Extension('CriCodecs', ["CriCodecs\\CriCodecs.cpp"], include_dirs=[os.path.realpath("CriCodecs")])]
)