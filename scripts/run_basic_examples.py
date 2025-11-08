#!/usr/bin/env python3
"""
Script to run basic examples of the 3D Computer Vision library.
"""

import sys
import os

# Add the src directory to the Python path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

from src.examples.basic_examples import point_cloud_rotation_example, image_processing_example
from src.examples.plane_fitting import plane_fitting_example

def main():
    """Run all basic examples"""
    print("Running 3D Computer Vision Examples")
    print("=" * 40)
    
    # Run point cloud rotation example
    point_cloud_rotation_example()
    
    # # Run image processing example
    # image_processing_example()
    
    # Run plane fitting example
    plane_fitting_example()
    
    print("All examples completed successfully!")

if __name__ == "__main__":
    main()
