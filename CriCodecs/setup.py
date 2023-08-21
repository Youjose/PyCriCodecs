from setuptools import setup, Extension

setup(
    name="CriCodecs",
    version="0.2.1",
    ext_modules=[Extension('CriCodecs', ["adx.cpp", "CriCodecs.cpp", "crilayla.cpp", "hca.cpp"])]
)