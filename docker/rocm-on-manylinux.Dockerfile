# Start with the standard Manylinux image (CentOS 7 based)
FROM quay.io/pypa/manylinux2014_x86_64

# 1. Fix CentOS 7 EOL Repos (Mandatory for Manylinux2014)
RUN sed -i 's/mirrorlist/#mirrorlist/g' /etc/yum.repos.d/CentOS-* && \
    sed -i 's|#baseurl=http://mirror.centos.org|baseurl=http://vault.centos.org|g' /etc/yum.repos.d/CentOS-*

# 2. Disable EPEL (metalink is dead for CentOS 7 EOL, and we don't need it)
RUN sed -i 's/enabled=1/enabled=0/g' /etc/yum.repos.d/epel.repo || true

# 3. Add ROCm repository
RUN printf "[ROCm]\nname=ROCm\nbaseurl=https://repo.radeon.com/rocm/yum/6.0/main\nenabled=1\ngpgcheck=0\n" \
    > /etc/yum.repos.d/rocm.repo

# 4. Install HIP runtime + devel headers (--nodeps to skip runtime deps unavailable on CentOS 7)
RUN yum install -y yum-utils curl && \
    yumdownloader hip-runtime-amd hip-devel && \
    rpm -ivh --nodeps hip-runtime-amd-*.rpm hip-devel-*.rpm && \
    rm -f hip-*.rpm

# 5. Ensure /opt/rocm symlink exists
RUN ln -sfn /opt/rocm-* /opt/rocm 2>/dev/null; \
    ls /opt/rocm/include/hip/hip_runtime.h

# 6. Create stub libamdhip64.so for linking if the real one isn't usable
RUN if ! readelf -h /opt/rocm/lib/libamdhip64.so >/dev/null 2>&1; then \
        gcc -shared -fPIC -Wl,-soname,libamdhip64.so.6 -o /opt/rocm/lib/libamdhip64.so.6 -x c /dev/null && \
        ln -sf libamdhip64.so.6 /opt/rocm/lib/libamdhip64.so; \
    fi
