#!/usr/bin/env python3

import cv2
import depthai as dai
import argparse

parser = argparse.ArgumentParser()
parser.add_argument("-s", "--source", default="recordings/recording.tar.gz", help="Recording path")
args = parser.parse_args()

# Create pipeline
with dai.Pipeline(True) as pipeline:
    # Define source and output
    camRgb = pipeline.create(dai.node.ColorCamera)

    # Properties
    camRgb.setBoardSocket(dai.CameraBoardSocket.CAM_A)
    camRgb.setResolution(dai.ColorCameraProperties.SensorResolution.THE_1080_P)
    camRgb.setVideoSize(1920, 1080)
    camRgb.setFps(30)

    imu = pipeline.create(dai.node.IMU)
    imu.enableIMUSensor(dai.IMUSensor.ACCELEROMETER_RAW, 500);
    imu.enableIMUSensor(dai.IMUSensor.GYROSCOPE_RAW, 400);

    pipeline.enableHolisticReplay(args.source)

    videoQueue = camRgb.preview.createOutputQueue()
    imuQueue = imu.out.createOutputQueue()

    # Connect to device and start pipeline
    pipeline.start()
    while pipeline.isRunning():
        videoIn : dai.ImgFrame = videoQueue.get()
        imuData : dai.IMUData = imuQueue.get()

        # Get BGR frame from NV12 encoded video frame to show with opencv
        # Visualizing the frame on slower hosts might have overhead
        cv2.imshow("video", videoIn.getCvFrame())

        for packet in imuData.packets:
            print(f"IMU Accelerometer: {packet.acceleroMeter}")
            print(f"IMU Gyroscope: {packet.gyroscope}")

        if cv2.waitKey(1) == ord('q'):
            break
