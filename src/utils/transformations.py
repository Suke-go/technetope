import numpy as np
import math
from typing import Tuple

class Transformations:
    @staticmethod
    def create_rotation_matrix(rotation_x: float, rotation_y: float, rotation_z: float = 0.0) -> np.ndarray:
        """Create combined rotation matrix from Euler angles"""
        # Rotation around X-axis
        rx = np.array([[1, 0, 0],
                      [0, math.cos(rotation_x), -math.sin(rotation_x)],
                      [0, math.sin(rotation_x), math.cos(rotation_x)]])
        
        # Rotation around Y-axis
        ry = np.array([[math.cos(rotation_y), 0, math.sin(rotation_y)],
                      [0, 1, 0],
                      [-math.sin(rotation_y), 0, math.cos(rotation_y)]])
        
        # Rotation around Z-axis
        rz = np.array([[math.cos(rotation_z), -math.sin(rotation_z), 0],
                      [math.sin(rotation_z), math.cos(rotation_z), 0],
                      [0, 0, 1]])
        
        # Combine rotations: R = Rz * Ry * Rx
        return np.matmul(rz, np.matmul(ry, rx))
    
    @staticmethod
    def apply_rotation(points: np.ndarray, rotation_matrix: np.ndarray) -> np.ndarray:
        """Apply rotation matrix to points"""
        return np.dot(points, rotation_matrix.T)
    
    @staticmethod
    def calculate_rotation_angles(points: np.ndarray) -> Tuple[float, float]:
        """Calculate rotation angles from point cloud using PCA"""
        centroid = np.mean(points, axis=0)
        centered_points = points - centroid
        
        covariance_matrix = np.dot(centered_points.T, centered_points)
        u, s, vh = np.linalg.svd(covariance_matrix)
        
        rotation_x = math.atan2(u[2, 1], u[2, 2])
        rotation_y = math.atan2(-u[2, 0], math.sqrt(u[2, 1]**2 + u[2, 2]**2))
        
        return rotation_x, rotation_y
    
    @staticmethod
    def degrees_to_radians(degrees: float) -> float:
        """Convert degrees to radians"""
        return np.deg2rad(degrees)
    
    @staticmethod
    def radians_to_degrees(radians: float) -> float:
        """Convert radians to degrees"""
        return np.rad2deg(radians)