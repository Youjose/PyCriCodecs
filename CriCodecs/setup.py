from setuptools import setup, Extension

setup(
    name="CriCodecs",
    version="0.1.0",
    ext_modules=[Extension('CriCodecs', ["adx.cpp", "CriCodecs.cpp", "crilayla.cpp", "hca.cpp"])]
)