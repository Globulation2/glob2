const {defineConfig}=require("/home/bradley/.codex/worktrees/83ab/glob2/platform/node_modules/@playwright/test");
module.exports=defineConfig({
  "testDir": "/home/bradley/.codex/worktrees/83ab/glob2/platform/apps/web/e2e",
  "testMatch": "i18n.spec.ts",
  "outputDir": "/home/bradley/.codex/worktrees/83ab/glob2/artifacts/bengali-localization-v7/web-results",
  "timeout": 120000,
  "workers": 1,
  "retries": 0,
  "reporter": [
    [
      "list"
    ],
    [
      "json",
      {
        "outputFile": "/home/bradley/.codex/worktrees/83ab/glob2/artifacts/bengali-localization-v7/web-e2e-results.json"
      }
    ]
  ],
  "use": {
    "baseURL": "http://127.0.0.1:51701",
    "trace": "retain-on-failure"
  },
  "projects": [
    {
      "name": "chromium-desktop",
      "use": {
        "browserName": "chromium",
        "viewport": {
          "width": 1280,
          "height": 860
        },
        "deviceScaleFactor": 1,
        "hasTouch": false
      }
    },
    {
      "name": "chromium-phone",
      "use": {
        "browserName": "chromium",
        "viewport": {
          "width": 412,
          "height": 839
        },
        "deviceScaleFactor": 2,
        "hasTouch": true
      }
    },
    {
      "name": "firefox-desktop",
      "use": {
        "browserName": "firefox",
        "viewport": {
          "width": 1280,
          "height": 860
        },
        "deviceScaleFactor": 1,
        "hasTouch": false
      }
    },
    {
      "name": "firefox-phone",
      "use": {
        "browserName": "firefox",
        "viewport": {
          "width": 412,
          "height": 839
        },
        "deviceScaleFactor": 2,
        "hasTouch": true
      }
    },
    {
      "name": "webkit-desktop",
      "use": {
        "browserName": "webkit",
        "viewport": {
          "width": 1280,
          "height": 860
        },
        "deviceScaleFactor": 1,
        "hasTouch": false
      }
    },
    {
      "name": "webkit-phone",
      "use": {
        "browserName": "webkit",
        "viewport": {
          "width": 412,
          "height": 839
        },
        "deviceScaleFactor": 2,
        "hasTouch": true
      }
    }
  ],
  "webServer": {
    "command": "node e2e/server.ts",
    "cwd": "/home/bradley/.codex/worktrees/83ab/glob2/platform/apps/web",
    "url": "http://127.0.0.1:51701/api/v1/instance",
    "timeout": 120000,
    "reuseExistingServer": false,
    "env": {
      "PORT": "51701"
    }
  }
});
