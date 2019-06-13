#!/usr/bin/env groovy

@Library('caf-continuous-integration') _

// Default CMake flags for release builds.
defaultReleaseBuildFlags = [
    'CAF_ENABLE_RUNTIME_CHECKS:BOOL=yes',
]

// Default CMake flags for debug builds.
defaultDebugBuildFlags = defaultReleaseBuildFlags + [
    'CAF_ENABLE_ADDRESS_SANITIZER:BOOL=yes',
    'CAF_LOG_LEVEL:STRING=TRACE',
]

// Configures the behavior of our stages.
config = [
    // GitHub path to repository.
    repository: 'actor-framework/incubator',
    // List of enabled checks for email notifications.
    checks: [
        'build',
        'style',
        'tests',
        'coverage',
    ],
    // Dependencies that we need to fetch before each build.
    dependencies: [
        artifact: [
            'CAF/actor-framework/master',
        ],
        cmakeRootVariables: [
            'CAF_ROOT_DIR',
        ],
    ],
    // Our build matrix. Keys are the operating system labels and values are build configurations.
    buildMatrix: [
        ['Linux', [
            builds: ['debug'],
            tools: ['gcc4.8', 'gcc4.9', 'gcc5', 'gcc6', 'gcc7'],
        ]],
        ['Linux', [
            builds: ['debug'],
            tools: ['gcc8'],
            extraSteps: ['coverageReport'],
        ]],
        ['Linux', [
            builds: ['release'],
            tools: ['gcc8'],
        ]],
        ['macOS', [
            builds: ['debug', 'release'],
            tools: ['clang'],
        ]],
        ['FreeBSD', [
            builds: ['debug', 'release'],
            tools: ['clang'],
        ]],
        ['Windows', [
            // TODO: debug build currently broken
            //builds: ['debug', 'release'],
            builds: ['release'],
            tools: ['msvc'],
        ]],
    ],
    // Platform-specific environment settings.
    buildEnvironments: [
        nop: [], // Dummy value for getting the proper types.
    ],
    // Default CMake flags by build type.
    defaultBuildFlags: [
        debug: defaultDebugBuildFlags,
        release: defaultReleaseBuildFlags,
    ],
    // CMake flags by OS and build type to override defaults for individual builds.
    buildFlags: [
        Windows: [
            debug: defaultDebugBuildFlags + [
                'CAF_BUILD_STATIC_ONLY:BOOL=yes',
            ],
            release: defaultReleaseBuildFlags + [
                'CAF_BUILD_STATIC_ONLY:BOOL=yes',
            ],
        ],
    ],
    // Configures what binary the coverage report uses and what paths to exclude.
    coverage: [
        binary: 'build/incubator-test',
        relativeExcludePaths: [
        ],
    ],
]

// Declarative pipeline for triggering all stages.
pipeline {
    options {
        buildDiscarder(logRotator(numToKeepStr: '50', artifactNumToKeepStr: '3'))
    }
    triggers {
        upstream(upstreamProjects: 'CAF/actor-framework/master/', threshold: hudson.model.Result.SUCCESS)
    }
    agent {
        label 'master'
    }
    environment {
        PrettyJobBaseName = env.JOB_BASE_NAME.replace('%2F', '/')
        PrettyJobName = "Incubator/$PrettyJobBaseName #${env.BUILD_NUMBER}"
        ASAN_OPTIONS = 'detect_leaks=0'
    }
    stages {
        stage('Checkout') {
            steps {
                getSources(config)
            }
        }
        stage('Lint') {
            agent { label 'clang-format' }
            steps {
                runClangFormat(config)
            }
        }
        stage('Build') {
            steps {
                buildParallel(config, PrettyJobBaseName)
            }
        }
        stage('Notify') {
            steps {
                collectResults(config, PrettyJobName)
            }
        }
    }
    post {
        failure {
            emailext(
                subject: "$PrettyJobName: " + config['checks'].collect{ "⛔️ ${it}" }.join(', '),
                recipientProviders: [culprits(), developers(), requestor(), upstreamDevelopers()],
                attachLog: true,
                compressLog: true,
                body: "Check console output at ${env.BUILD_URL} or see attached log.\n",
            )
            notifyAllChecks(config, 'failure', 'Failed due to earlier error')
        }
    }
}
