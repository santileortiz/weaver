from setuptools import setup, find_packages
import os

print (find_packages())
print (os.environ)

setup(
    name='app_dist',
    version='0.1',
    packages=['server', 'mkpy'],
    include_package_data=True,
    install_requires=[
        'flask'
    ],
)
