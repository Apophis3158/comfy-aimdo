# Start with the standard Manylinux image (CentOS 7 based)
FROM quay.io/pypa/manylinux2014_x86_64

# 1. Fix CentOS 7 EOL Repos (Mandatory for Manylinux2014)
RUN sed -i 's/mirrorlist/#mirrorlist/g' /etc/yum.repos.d/CentOS-* && \
    sed -i 's|#baseurl=http://mirror.centos.org|baseurl=http://vault.centos.org|g' /etc/yum.repos.d/CentOS-*

RUN yum install -y \
    curl
