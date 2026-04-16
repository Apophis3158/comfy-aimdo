# Start with the standard Manylinux image (CentOS 7 based)
FROM quay.io/pypa/manylinux2014_x86_64

# 1. Fix CentOS 7 EOL Repos (Mandatory for Manylinux2014)
RUN sed -i 's/mirrorlist/#mirrorlist/g' /etc/yum.repos.d/CentOS-* && \
    sed -i 's|#baseurl=http://mirror.centos.org|baseurl=http://vault.centos.org|g' /etc/yum.repos.d/CentOS-*

RUN yum-config-manager --add-repo https://developer.download.nvidia.com/compute/cuda/repos/rhel7/x86_64/cuda-rhel7.repo

RUN yum install -y \
    curl \
    cuda-nvcc-12-1 \
    cuda-cudart-devel-12-1 \
    cuda-driver-devel-12-1

RUN STUB_DIR=/usr/local/cuda-12.1/targets/x86_64-linux/lib/stubs && \
    rm -f $STUB_DIR/libcuda.so && \
    gcc -shared -fPIC -Wl,-soname,libcuda.so.1 -o $STUB_DIR/libcuda.so.1 -x c /dev/null && \
    ln -sf $STUB_DIR/libcuda.so.1 $STUB_DIR/libcuda.so
