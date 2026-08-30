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

    // Publishing only happens for a tagged commit. Every push builds and
    // archives into Jenkins; only `git tag -a v1.2.0 && git push origin v1.2.0`
    // produces a GitHub Release. Otherwise every commit would create one.
    //
    // The tag is detected from the commit rather than from a branch spec, so
    // this works whether the build was triggered by the tag push itself or by
    // the push that preceded it.
    stage('Publish GitHub Release') {
      when {
        expression {
          env.RELEASE_TAG = sh(
            script: "git tag --points-at HEAD | grep -E '^v' | head -1",
            returnStdout: true
          ).trim()
          if (env.RELEASE_TAG) {
            echo "HEAD is tagged ${env.RELEASE_TAG} - publishing a release"
            return true
          }
          echo 'HEAD carries no v* tag - skipping release (this is the normal path)'
          return false
        }
      }
      steps {
        withCredentials([string(credentialsId: 'github-pat', variable: 'GH_TOKEN')]) {
          sh '''
            set -eu
            REPO="Rozakos/Dot-Matrix-Stock-Ticker"
            API="https://api.github.com/repos/${REPO}"
            ASSET="dot-matrix-stock-ticker-${RELEASE_TAG}.bin"
            cp .pio/build/esp32/firmware.bin "${ASSET}"

            # Idempotent: re-running a build on an already-released tag must not
            # fail, and must not create a duplicate.
            EXISTING=$(curl -sS -H "Authorization: Bearer ${GH_TOKEN}" \
                        -H "Accept: application/vnd.github+json" \
                        "${API}/releases/tags/${RELEASE_TAG}" \
                        | sed -n 's/.*"id": \\([0-9]*\\).*/\\1/p' | head -1)

            if [ -n "${EXISTING}" ]; then
              echo "release for ${RELEASE_TAG} already exists (id ${EXISTING}) - reusing it"
              RELEASE_ID="${EXISTING}"
            else
              SIZE=$(stat -c%s .pio/build/esp32/firmware.bin)
              BODY="Automated build from Jenkins.\\n\\nfirmware.bin: ${SIZE} bytes\\nCommit: $(git rev-parse --short HEAD)"
              RELEASE_ID=$(curl -sS -X POST \
                -H "Authorization: Bearer ${GH_TOKEN}" \
                -H "Accept: application/vnd.github+json" \
                -d "{\\"tag_name\\":\\"${RELEASE_TAG}\\",\\"name\\":\\"${RELEASE_TAG}\\",\\"body\\":\\"${BODY}\\"}" \
                "${API}/releases" \
                | sed -n 's/.*"id": \\([0-9]*\\).*/\\1/p' | head -1)
              echo "created release id ${RELEASE_ID}"
            fi

            if [ -z "${RELEASE_ID}" ]; then
              echo "FAILED: could not determine a release id"
              exit 1
            fi

            # Replace the asset if a previous run of this tag already uploaded it.
            OLD=$(curl -sS -H "Authorization: Bearer ${GH_TOKEN}" \
                   -H "Accept: application/vnd.github+json" \
                   "${API}/releases/${RELEASE_ID}/assets" \
                   | tr "}" "\\n" | grep -F "\\"name\\": \\"${ASSET}\\"" \
                   | sed -n 's/.*"id": \\([0-9]*\\).*/\\1/p' | head -1)
            if [ -n "${OLD}" ]; then
              echo "replacing existing asset ${OLD}"
              curl -sS -X DELETE -H "Authorization: Bearer ${GH_TOKEN}" \
                "${API}/releases/assets/${OLD}" >/dev/null
            fi

            HTTP=$(curl -sS -o /tmp/upload.json -w "%{http_code}" -X POST \
              -H "Authorization: Bearer ${GH_TOKEN}" \
              -H "Content-Type: application/octet-stream" \
              --data-binary @"${ASSET}" \
              "https://uploads.github.com/repos/${REPO}/releases/${RELEASE_ID}/assets?name=${ASSET}")

            if [ "${HTTP}" != "201" ]; then
              echo "FAILED: asset upload returned HTTP ${HTTP}"
              head -c 400 /tmp/upload.json
              exit 1
            fi
            echo "published ${ASSET} to release ${RELEASE_TAG}"
            sed -n 's/.*"browser_download_url": "\\([^"]*\\)".*/  \\1/p' /tmp/upload.json | head -1
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
