# High Level Simulation

This project provides a high-level simulation environment using SystemC.

## SystemC Installation

### Prerequisites

**Fedora**

```bash
sudo dnf install cmake clang
sudo dnf install glibc-static libstdc++-static  # Required for static linking
```

### Installation Steps

1. **Clone the SystemC Repository**  
   Clone the official SystemC repository from Accellera:

   ```bash
   git clone https://github.com/accellera-official/systemc.git
   ```

2. **Create a Build Directory**  
   Change into the SystemC directory and create a build subdirectory:

   ```bash
   cd systemc
   mkdir build
   cd build
   ```

3. **Generate the Makefiles using CMake**  
   Configure the build to install SystemC into a local directory (`../install`) and build using static libraries:

   ```bash
   cmake .. -DCMAKE_INSTALL_PREFIX=../install -DBUILD_SHARED_LIBS=OFF
   ```

4. **Compile the Source**  
   Compile the project using all available CPU cores:

   ```bash
   make -j$(nproc)
   ```

5. **Install SystemC**  
   Install the compiled library files:

   ```bash
   make install
   ```

### Environment Variables

`~/.bashrc`

```bash
export SYSTEMC_PATH=/path/to/systemc/install
export LD_LIBRARY_PATH=$SYSTEMC_PATH/lib:$LD_LIBRARY_PATH
```


```bash
source ~/.bashrc
```

## Compilation

```bash
make
```

## Model Information