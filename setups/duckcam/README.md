## Duckiebot Camera

This setup simulates a simple camera processing pipeline using the **Duckiebot** image.  
It reads the `duckiebot_input.jpg` image, crops it, converts it to grayscale, and saves the results as sequentially numbered output files (`duckiebot_outputX.jpg`).

The process repeats every **8 ms** (= 125 FPS) for **ten frames**, emulating a real-time video stream.