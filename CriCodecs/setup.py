from setuptools import setup, Extension

setup(
    name="CriCodecs",
    version="0.0.1",
    ext_modules=[Extension('CriCodecs', ["adx.cpp", "CriCodecs.cpp", "crilayla.cpp"])]
)