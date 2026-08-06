# Licenses

ChipleX itself is released under the MIT License, reproduced below. ChipleX
incorporates third-party components in its source tree and, in the prebuilt
release, bundles further third-party libraries. Each such component remains
under its own license. The relevant licenses and copyright notices are
reproduced or referenced in the sections that follow.

## ChipleX

MIT License

Copyright (c) 2026 Technical University of Munich

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

# Third-party components in the source tree

## ARM AXI4 TLM modeling (`include/ARM/`, `src/ARM/`)

The AXI4 transaction-level modeling code is vendored from Arm and distributed
under The Clear BSD License.

The Clear BSD License

Copyright (c) 2015-2021 Arm Limited.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted (subject to the limitations in the disclaimer
below) provided that the following conditions are met:

     * Redistributions of source code must retain the above copyright notice,
     this list of conditions and the following disclaimer.

     * Redistributions in binary form must reproduce the above copyright
     notice, this list of conditions and the following disclaimer in the
     documentation and/or other materials provided with the distribution.

     * Neither the name of the copyright holder nor the names of its
     contributors may be used to endorse or promote products derived from
     this software without specific prior written permission.

NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED BY
THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT
NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Third-party components linked or bundled by the build

## yaml-cpp

Fetched at configure time and statically linked into the simulator. Distributed
under the MIT License.

Copyright (c) 2008-2015 Jesse Beder

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## SystemC (Accellera Systems Initiative)

Built from the upstream release and bundled with the prebuilt application
(`libsystemc.so`). Distributed under the Apache License, Version 2.0.

Copyright (c) 1996-2022 by all Contributors to the Accellera SystemC standard.

The full text of the Apache License, Version 2.0 is available at
https://www.apache.org/licenses/LICENSE-2.0 and is included with the SystemC
distribution.

## Qt and PySide6

Bundled with the prebuilt application. The Qt libraries and the PySide6 Python
bindings are used under the GNU Lesser General Public License, version 3
(LGPL-3.0).

Copyright (c) The Qt Company Ltd. and other Qt/PySide contributors.

The full text of the LGPL-3.0 (together with the GPL-3.0 it references) is
available at https://www.gnu.org/licenses/lgpl-3.0.html. As required by the
LGPL, the bundled Qt libraries may be replaced by the end user; they are shipped
as separate shared libraries within the release.

## NumPy

Bundled with the prebuilt application. Distributed under the BSD 3-Clause
License.

Copyright (c) 2005-2024, NumPy Developers.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the copyright holder nor the names of its contributors
  may be used to endorse or promote products derived from this software without
  specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

## pyqtgraph

Bundled with the prebuilt application. Distributed under the MIT License.
Copyright (c) pyqtgraph developers. Full text at
https://github.com/pyqtgraph/pyqtgraph/blob/master/LICENSE.txt

## PyYAML

Bundled with the prebuilt application. Distributed under the MIT License.
Copyright (c) 2017-2021 Ingy dot Net; Copyright (c) 2006-2016 Kirill Simonov.
Full text at https://github.com/yaml/pyyaml/blob/main/LICENSE

## PyInstaller (bootloader)

The prebuilt application is packaged with PyInstaller, whose bootloader is
included in the resulting executable. PyInstaller is licensed under the GNU
General Public License, version 2 or later, with an exception that permits the
resulting bundled application to be distributed under terms of the user's
choosing. See https://github.com/pyinstaller/pyinstaller/blob/develop/COPYING.txt

# External tools (used at runtime, not distributed with ChipleX)

The cycle-estimation flow invokes the following tools if they are present on the
system. They are not included in this repository or in the prebuilt release, and
remain under their own licenses:

* gem5 - BSD 3-Clause License (with additional third-party components under
  their respective licenses). https://gem5.org/
* LLVM `llvm-mca` - Apache License 2.0 with LLVM exceptions.
  https://llvm.org/
* Workload compiler toolchain (for example the RISC-V GNU toolchain) - GCC is
  distributed under the GNU General Public License, version 3, with the GCC
  Runtime Library Exception. The exact license depends on the toolchain chosen
  for a given CPU model.
