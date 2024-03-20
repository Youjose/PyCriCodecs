from setuptools import setup, Extension
import os

setup(
    name="PyCriCodecs",
    version="0.4.8",
    description="Python frontend with a C++ backend of managing Criware files of all kinds.",
    packages=["PyCriCodecs"],
    ext_modules=[Extension(
        'CriCodecs',
        [os.path.join("CriCodecs", "CriCodecs.cpp")],
        include_dirs=[os.path.realpath("CriCodecs")],
        extra_compile_args=["-std=c++11", "-O3"]
        )]
)