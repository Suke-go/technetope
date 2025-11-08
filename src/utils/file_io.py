import os
import json
import numpy as np
import open3d as o3d
import cv2
from typing import Union, Optional

class FileIO:
    @staticmethod
    def ensure_directory_exists(directory: str) -> None:
        """Ensure a directory exists, create it if it doesn't"""
        if not os.path.exists(directory):
            os.makedirs(directory)
    
    @staticmethod
    def save_point_cloud(pcd: o3d.geometry.PointCloud, file_path: str) -> bool:
        """Save point cloud to file"""
        try:
            FileIO.ensure_directory_exists(os.path.dirname(file_path))
            return o3d.io.write_point_cloud(file_path, pcd)
        except Exception as e:
            print(f"Error saving point cloud: {e}")
            return False
    
    @staticmethod
    def load_point_cloud(file_path: str) -> Optional[o3d.geometry.PointCloud]:
        """Load point cloud from file"""
        try:
            if not os.path.exists(file_path):
                raise FileNotFoundError(f"Point cloud file not found: {file_path}")
            
            pcd = o3d.io.read_point_cloud(file_path)
            if not pcd.has_points():
                raise ValueError("Failed to load point cloud or file is empty")
            
            return pcd
        except Exception as e:
            print(f"Error loading point cloud: {e}")
            return None
    
    @staticmethod
    def save_image(image: np.ndarray, file_path: str) -> bool:
        """Save image to file"""
        try:
            FileIO.ensure_directory_exists(os.path.dirname(file_path))
            return cv2.imwrite(file_path, image)
        except Exception as e:
            print(f"Error saving image: {e}")
            return False
    
    @staticmethod
    def load_image(file_path: str) -> Optional[np.ndarray]:
        """Load image from file"""
        try:
            if not os.path.exists(file_path):
                raise FileNotFoundError(f"Image file not found: {file_path}")
            
            image = cv2.imread(file_path)
            if image is None:
                raise ValueError("Failed to load image or file is empty")
            
            return image
        except Exception as e:
            print(f"Error loading image: {e}")
            return None
    
    @staticmethod
    def save_json(data: Union[dict, list], file_path: str) -> bool:
        """Save data to JSON file"""
        try:
            FileIO.ensure_directory_exists(os.path.dirname(file_path))
            with open(file_path, 'w') as f:
                json.dump(data, f, indent=4)
            return True
        except Exception as e:
            print(f"Error saving JSON: {e}")
            return False
    
    @staticmethod
    def load_json(file_path: str) -> Optional[Union[dict, list]]:
        """Load data from JSON file"""
        try:
            if not os.path.exists(file_path):
                raise FileNotFoundError(f"JSON file not found: {file_path}")
            
            with open(file_path, 'r') as f:
                return json.load(f)
        except Exception as e:
            print(f"Error loading JSON: {e}")
            return None