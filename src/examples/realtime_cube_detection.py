import pyrealsense2 as rs
import cv2
import numpy as np
from typing import Optional, Tuple
import time

from src.core.image_processor import ImageProcessor
from src.utils.visualization import Visualizer

class RealTimeCubeDetector:
    def __init__(self, config: Optional[dict] = None):
        self.image_processor = ImageProcessor()
        self.pipeline = rs.pipeline()
        self.config = rs.config()
        
        # Configure streams
        self.config.enable_stream(rs.stream.depth, 640, 480, rs.format.z16, 30)
        self.config.enable_stream(rs.stream.color, 640, 480, rs.format.bgr8, 30)
        
        self.align = rs.align(rs.stream.color)
        self.pc = rs.pointcloud()
        
    def start_stream(self):
        """Start RealSense pipeline"""
        self.pipeline.start(self.config)
        # Warm-up frames
        for _ in range(20):
            self.pipeline.wait_for_frames()
    
    def stop_stream(self):
        """Stop RealSense pipeline"""
        self.pipeline.stop()
    
    def get_frames(self) -> Optional[Tuple[rs.depth_frame, rs.video_frame]]:
        """Get aligned depth and color frames"""
        try:
            frames = self.pipeline.wait_for_frames()
            aligned_frames = self.align.process(frames)
            
            depth_frame = aligned_frames.get_depth_frame()
            color_frame = aligned_frames.get_color_frame()
            
            if not depth_frame or not color_frame:
                return None
                
            return depth_frame, color_frame
            
        except Exception as e:
            print(f"Error getting frames: {e}")
            return None
    
    def process_frame(self, depth_frame: rs.depth_frame, color_frame: rs.video_frame) -> dict:
        """Process a single frame for cube detection"""
        # Convert to numpy arrays
        depth_image = np.asanyarray(depth_frame.get_data())
        color_image = np.asanyarray(color_frame.get_data())
        
        # Process image for blue detection
        hsv_image = self.image_processor.convert_to_hsv(color_image)
        blue_mask = self.image_processor.create_color_mask(hsv_image)
        
        # Find contours
        contours, _ = self.image_processor.find_contours(blue_mask)
        
        result = {
            'depth_image': depth_image,
            'color_image': color_image,
            'blue_mask': blue_mask,
            'contours': contours,
            'cube_detected': False
        }
        
        if contours:
            # Find largest contour
            largest_contour = max(contours, key=cv2.contourArea)
            if cv2.contourArea(largest_contour) > 500:
                result['cube_detected'] = True
                result['largest_contour'] = largest_contour
                
                # Get bounding box and depth information
                x, y, w, h = cv2.boundingRect(largest_contour)
                depth_roi = depth_image[y:y+h, x:x+w].astype(float)
                depth_value = np.median(depth_roi[depth_roi > 0])
                
                result['bounding_box'] = (x, y, w, h)
                result['depth'] = depth_value
        
        return result
    
    def run(self):
        """Main detection loop"""
        self.start_stream()
        
        try:
            while True:
                start_time = time.time()
                
                frames = self.get_frames()
                if not frames:
                    continue
                
                depth_frame, color_frame = frames
                result = self.process_frame(depth_frame, color_frame)
                
                # Display results
                self.display_results(result)
                
                # Calculate FPS
                fps = 1.0 / (time.time() - start_time)
                cv2.putText(result['color_image'], f"FPS: {fps:.1f}", 
                           (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 0), 2)
                
                if cv2.waitKey(1) & 0xFF == ord('q'):
                    break
                    
        finally:
            self.stop_stream()
            cv2.destroyAllWindows()
    
    def display_results(self, result: dict):
        """Display processing results"""
        color_image = result['color_image'].copy()
        
        if result['cube_detected']:
            x, y, w, h = result['bounding_box']
            cv2.rectangle(color_image, (x, y), (x + w, y + h), (0, 255, 0), 2)
            cv2.putText(color_image, f"Depth: {result['depth']:.2f}m", 
                       (x, y - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
        
        # Show images
        depth_colormap = cv2.applyColorMap(
            cv2.convertScaleAbs(result['depth_image'], alpha=0.03), cv2.COLORMAP_JET
        )
        
        cv2.imshow('Color Image', color_image)
        cv2.imshow('Depth', depth_colormap)
        cv2.imshow('Blue Mask', result['blue_mask'])