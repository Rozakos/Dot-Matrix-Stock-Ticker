// CI for the dot-matrix stock ticker (ESP32).
//
// Runs on the self-hosted Jenkins controller as a MULTIBRANCH pipeline, which
// matters for one specific reason: Jenkins' git polling uses `git ls-remote -h`,
// which lists heads only and cannot see tags at all. A plain pipeline-from-SCM
// job therefore never notices a pushed tag, whatever its branch specs say.
// Multibranch discovers tags as first-class jobs, so `git push origin v1.2.0`
// builds and publishes with no manual step.
//
// This project has no unit tests, so the pipeline is a build gate: prove the
// firmware still compiles from a clean clone, keep the image, report its size,
// and on a tag, publish it to GitHub Releases.
//
// The min_spiffs layout leaves roughly a 1.9 MB app slot - tighter than the
// huge_app layout CYD-Stock-Ticker had to move to. PlatformIO prints the exact
// budget as "Flash: x% (used N from M bytes)"; that line is the real check.

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
        // src/secrets.h is gitignored - .gitignore says so in as many words:
        // "Real credentials live in src/secrets.h - never commit them." So a
        // clean clone cannot compile without it, and CI copies the committed
        // template exactly as the README tells a human to.
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

    // Publishing happens ONLY on a tag build. Multibranch creates a separate job
    // per discovered tag and sets TAG_NAME there, so buildingTag() is an exact
    // signal. An earlier version checked whether HEAD carried a tag instead,
    // which also fired on the main job whenever main's head was the tagged
    // commit - and then failed trying to re-upload an asset that already existed.
    stage('Publish GitHub Release') {
      when { buildingTag() }
      steps {
        withCredentials([string(credentialsId: 'github-pat', variable: 'GH_TOKEN')]) {
          sh '''
            set -eu
            ASSET="dot-matrix-stock-ticker-${TAG_NAME}.bin"
            cp .pio/build/esp32/firmware.bin "$ASSET"
            python3 scripts/publish_release.py \
              --repo Rozakos/Dot-Matrix-Stock-Ticker \
              --tag "${TAG_NAME}" \
              --asset "$ASSET"
          '''
        }
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
