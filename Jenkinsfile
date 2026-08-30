// CI for the dot-matrix stock ticker (ESP32).
//
// Runs on the self-hosted Jenkins controller. This project has no unit tests,
// so the pipeline is a build gate: prove the firmware still compiles from a
// clean clone, keep the image, and report its size.
//
// The min_spiffs layout leaves roughly a 1.9 MB app slot, which is a good deal
// tighter than the huge_app layout CYD-Stock-Ticker had to move to. PlatformIO
// prints the exact budget as "Flash: x% (used N from M bytes)" during the build;
// that line is the authoritative check and the post step below just restates the
// artifact size next to it.

pipeline {
  agent any

  options {
    timestamps()
    timeout(time: 30, unit: 'MINUTES')
    disableConcurrentBuilds()
    buildDiscarder(logRotator(numToKeepStr: '20', artifactNumToKeepStr: '10'))
  }

  environment {
    // Shared ESP32 toolchain cache, kept outside the workspace so cleanWs()
    // does not trigger a multi-minute cold re-download.
    PLATFORMIO_CORE_DIR = "${JENKINS_HOME}/.platformio"
  }

  stages {
    stage('Checkout') {
      steps {
        checkout scm
        sh 'git --no-pager log -1 --oneline'
      }
    }

    stage('Stub secrets.h') {
      steps {
        // src/secrets.h is gitignored - .gitignore says so explicitly: "Real
        // credentials live in src/secrets.h - never commit them." So a clean
        // clone cannot compile without it, and CI copies the committed template
        // exactly as the README tells a human to.
        //
        // This proves the code COMPILES. The artifact holds placeholder
        // credentials and is not a drop-in image for a real network.
        sh '''
          if [ ! -f src/secrets.h ]; then
            cp src/secrets_example.h src/secrets.h
            echo "secrets.h stubbed from secrets_example.h"
          fi
        '''
      }
    }

    stage('Build firmware (esp32)') {
      steps {
        sh 'pio run -e esp32'
      }
    }

    stage('Archive firmware') {
      steps {
        sh 'ls -lh .pio/build/esp32/firmware.bin'
        archiveArtifacts artifacts: '.pio/build/esp32/firmware.*',
                         fingerprint: true,
                         onlyIfSuccessful: true
      }
    }
  }

  post {
    always {
      sh '''
        if [ -f .pio/build/esp32/firmware.bin ]; then
          size=$(stat -c%s .pio/build/esp32/firmware.bin)
          echo "firmware.bin = ${size} bytes ($((size / 1024)) KB) against the min_spiffs app slot - see the Flash: line above"
        fi
      '''
    }
    cleanup {
      cleanWs()
    }
  }
}
