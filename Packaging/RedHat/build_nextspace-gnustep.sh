#!/bin/sh
# -*-Shell-script-*-

. `dirname $0`/functions

if [ $# -eq 0 ];then
    print_help
    exit 1
fi

REPO_DIR=$1
SPEC_FILE=${REPO_DIR}/Libraries/gnustep/nextspace-gnustep.spec
GNUSTEP_VERSION=`rpm_version ${SPEC_FILE}`

print_H1 " Building NEXTSPACE GNUstep (nextspace-gnustep) package..."
cp ${REPO_DIR}/Libraries/gnustep/gdnc-local.service ${SOURCES_DIR}
cp ${REPO_DIR}/Libraries/gnustep/gdnc.service ${SOURCES_DIR}
cp ${REPO_DIR}/Libraries/gnustep/gdomap.interfaces ${SOURCES_DIR}
cp ${REPO_DIR}/Libraries/gnustep/gdomap.service ${SOURCES_DIR}
cp ${REPO_DIR}/Libraries/gnustep/gpbs.service ${SOURCES_DIR}
cp ${REPO_DIR}/Libraries/gnustep/gnustep-gui-images.tar.gz ${SOURCES_DIR}
cp ${REPO_DIR}/Libraries/gnustep/gnustep-gui-panels.tar.gz ${SOURCES_DIR}
cp ${REPO_DIR}/Libraries/gnustep/gorm-images.tar.gz ${SOURCES_DIR}
cp ${REPO_DIR}/Libraries/gnustep/projectcenter-images.tar.gz ${SOURCES_DIR}
cp ${REPO_DIR}/Libraries/gnustep/pc.patch ${SOURCES_DIR}
cp ${REPO_DIR}/Libraries/gnustep/gorm.patch ${SOURCES_DIR}
cp ${REPO_DIR}/Libraries/gnustep/libs-back.patch ${SOURCES_DIR}
cp ${REPO_DIR}/Libraries/gnustep/libs-gui.patch ${SOURCES_DIR}
print_H1 " Downloading GNUstep Base..."
curl -L https://github.com/gnustep/libs-base/archive/master.tar.gz -o ${SOURCES_DIR}/libs-base-master.tar.gz
print_H1 " Downloading GNUstep Back..."
curl -L https://github.com/gnustep/libs-back/archive/master.tar.gz -o ${SOURCES_DIR}/libs-back-master.tar.gz
print_H1 " Downloading GNUstep GUI..."
curl -L https://github.com/gnustep/libs-gui/archive/master.tar.gz -o ${SOURCES_DIR}/libs-gui-master.tar.gz
print_H1 " Downloading GNUstep ProjectCenter..."
curl -L https://github.com/gnustep/apps-projectcenter/archive/master.tar.gz -o ${SOURCES_DIR}/projectcenter-master.tar.gz
print_H1 " Downloading GNUstep GORM..."
curl -L https://github.com/gnustep/apps-gorm/archive/master.tar.gz -o ${SOURCES_DIR}/gorm-master.tar.gz

print_H2 "===== Install GNUstep build dependencies..."
DEPS=`rpmspec -q --buildrequires ${SPEC_FILE} | awk -c '{print $1}'`
sudo yum -y install ${DEPS}

print_H2 "===== Downloading GNUstep sources..."
spectool -g -R ${SPEC_FILE}

print_H2 "===== Building GNUstep package..."
rpmbuild -bb ${SPEC_FILE}
STATUS=$?
if [ $STATUS -eq 0 ]; then 
    print_OK " Building of NEXTSPACE GNUstep RPM SUCCEEDED!"

    install_rpm nextspace-gnustep ${RPMS_DIR}/nextspace-gnustep-${GNUSTEP_VERSION}.rpm
    mv ${RPMS_DIR}/nextspace-gnustep-${GNUSTEP_VERSION}.rpm ${RELEASE_USR}

    install_rpm nextspace-gnustep-devel ${RPMS_DIR}/nextspace-gnustep-devel-${GNUSTEP_VERSION}.rpm
    mv ${RPMS_DIR}/nextspace-gnustep-devel-${GNUSTEP_VERSION}.rpm ${RELEASE_DEV}
    mv ${RPMS_DIR}/nextspace-gnustep-debuginfo-${GNUSTEP_VERSION}.rpm ${RELEASE_DEV}
    if [ -f ${RPMS_DIR}/nextspace-gnustep-debugsource-${GNUSTEP_VERSION}.rpm ];then
        mv ${RPMS_DIR}/nextspace-gnustep-debugsource-${GNUSTEP_VERSION}.rpm ${RELEASE_DEV}
    fi
else
    print_ERR " Building of NEXTSPACE GNUstep RPM FAILED!"
    exit $STATUS
fi
