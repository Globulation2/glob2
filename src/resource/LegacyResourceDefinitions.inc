// SPDX-License-Identifier: GPL-3.0-or-later
// Frozen pre-139 resource identities and definitions. Only the legacy save
// adapter uses this content; installed catalogs must never reinterpret old IDs.
R"legacy_resources(
{
  "schemaVersion": 1,
  "resources": [
    {
      "key": "trees",
      "properties": {
        "primaryMaterial": "wood",
        "growthRate": 196608,
        "spreadRate": 196608,
        "ecology": "land",
        "habitatMask": 1,
        "stockBranchDivisor": 8,
        "stockDependentGrowth": true,
        "blocksGround": true,
        "blocksAir": false,
        "blocksBuilding": true,
        "clearable": true,
        "visibleToHarvest": false,
        "persistsWhenEmpty": false,
        "farmable": false,
        "smoothPlacement": true,
        "clearConsumption": "all",
        "requiresGrowthTerrain": true,
        "requiresPermanentDepositsTerrain": false
      },
      "yields": {
        "wood": {
          "capacity": 5,
          "initial": 1,
          "seedReserve": 1,
          "growthRate": 196608,
          "consumption": "all",
          "destroysDeposit": true,
          "placementMaximum": 4
        }
      },
      "requiredExperiment": "",
      "presentation": {
        "name": "Trees",
        "sprite": "data/gfx/ressource",
        "minimap": [
          0,
          60,
          0
        ],
        "levels": [
          {
            "stock": 0,
            "variants": [
              {
                "frame": 0,
                "weight": 1
              },
              {
                "frame": 5,
                "weight": 1
              }
            ]
          },
          {
            "stock": 1,
            "variants": [
              {
                "frame": 0,
                "weight": 1
              },
              {
                "frame": 5,
                "weight": 1
              }
            ]
          },
          {
            "stock": 2,
            "variants": [
              {
                "frame": 1,
                "weight": 1
              },
              {
                "frame": 6,
                "weight": 1
              }
            ]
          },
          {
            "stock": 3,
            "variants": [
              {
                "frame": 2,
                "weight": 1
              },
              {
                "frame": 7,
                "weight": 1
              }
            ]
          },
          {
            "stock": 4,
            "variants": [
              {
                "frame": 3,
                "weight": 1
              },
              {
                "frame": 8,
                "weight": 1
              }
            ]
          },
          {
            "stock": 5,
            "variants": [
              {
                "frame": 4,
                "weight": 1
              },
              {
                "frame": 9,
                "weight": 1
              }
            ]
          }
        ],
        "animationFrames": 1,
        "animationTicks": 1,
        "animationStride": 1
      }
    },
    {
      "key": "wheat",
      "properties": {
        "primaryMaterial": "food",
        "growthRate": 65536,
        "spreadRate": 196608,
        "ecology": "land",
        "habitatMask": 1,
        "stockBranchDivisor": 8,
        "stockDependentGrowth": true,
        "blocksGround": true,
        "blocksAir": false,
        "blocksBuilding": true,
        "clearable": true,
        "visibleToHarvest": false,
        "persistsWhenEmpty": false,
        "farmable": true,
        "smoothPlacement": true,
        "clearConsumption": "one",
        "requiresGrowthTerrain": true,
        "requiresPermanentDepositsTerrain": false
      },
      "yields": {
        "food": {
          "capacity": 5,
          "initial": 1,
          "seedReserve": 1,
          "growthRate": 196608,
          "consumption": "one",
          "destroysDeposit": false,
          "placementMaximum": 4
        }
      },
      "requiredExperiment": "",
      "presentation": {
        "name": "Wheat",
        "sprite": "data/gfx/ressource",
        "minimap": [
          211,
          207,
          167
        ],
        "levels": [
          {
            "stock": 0,
            "variants": [
              {
                "frame": 10,
                "weight": 1
              },
              {
                "frame": 15,
                "weight": 1
              }
            ]
          },
          {
            "stock": 1,
            "variants": [
              {
                "frame": 10,
                "weight": 1
              },
              {
                "frame": 15,
                "weight": 1
              }
            ]
          },
          {
            "stock": 2,
            "variants": [
              {
                "frame": 11,
                "weight": 1
              },
              {
                "frame": 16,
                "weight": 1
              }
            ]
          },
          {
            "stock": 3,
            "variants": [
              {
                "frame": 12,
                "weight": 1
              },
              {
                "frame": 17,
                "weight": 1
              }
            ]
          },
          {
            "stock": 4,
            "variants": [
              {
                "frame": 13,
                "weight": 1
              },
              {
                "frame": 18,
                "weight": 1
              }
            ]
          },
          {
            "stock": 5,
            "variants": [
              {
                "frame": 14,
                "weight": 1
              },
              {
                "frame": 19,
                "weight": 1
              }
            ]
          }
        ],
        "animationFrames": 1,
        "animationTicks": 1,
        "animationStride": 1
      }
    },
    {
      "key": "papyrus",
      "properties": {
        "primaryMaterial": "paper",
        "growthRate": 196608,
        "spreadRate": 0,
        "ecology": "uniform",
        "habitatMask": 1,
        "stockBranchDivisor": 8,
        "stockDependentGrowth": true,
        "blocksGround": true,
        "blocksAir": false,
        "blocksBuilding": true,
        "clearable": true,
        "visibleToHarvest": false,
        "persistsWhenEmpty": false,
        "farmable": false,
        "smoothPlacement": false,
        "clearConsumption": "one",
        "requiresGrowthTerrain": true,
        "requiresPermanentDepositsTerrain": false
      },
      "yields": {
        "paper": {
          "capacity": 5,
          "initial": 1,
          "seedReserve": 1,
          "growthRate": 196608,
          "consumption": "one",
          "destroysDeposit": false,
          "placementMaximum": 4
        }
      },
      "requiredExperiment": "",
      "presentation": {
        "name": "Papyrus",
        "sprite": "data/gfx/ressource",
        "minimap": [
          0,
          0,
          0
        ],
        "levels": [
          {
            "stock": 0,
            "variants": [
              {
                "frame": 20,
                "weight": 1
              }
            ]
          },
          {
            "stock": 1,
            "variants": [
              {
                "frame": 20,
                "weight": 1
              }
            ]
          },
          {
            "stock": 2,
            "variants": [
              {
                "frame": 21,
                "weight": 1
              }
            ]
          },
          {
            "stock": 3,
            "variants": [
              {
                "frame": 22,
                "weight": 1
              }
            ]
          },
          {
            "stock": 4,
            "variants": [
              {
                "frame": 23,
                "weight": 1
              }
            ]
          },
          {
            "stock": 5,
            "variants": [
              {
                "frame": 24,
                "weight": 1
              }
            ]
          }
        ],
        "animationFrames": 1,
        "animationTicks": 1,
        "animationStride": 1
      }
    },
    {
      "key": "rocks",
      "properties": {
        "primaryMaterial": "stone",
        "growthRate": 0,
        "spreadRate": 0,
        "ecology": "none",
        "habitatMask": 1,
        "stockBranchDivisor": 8,
        "stockDependentGrowth": true,
        "blocksGround": true,
        "blocksAir": false,
        "blocksBuilding": true,
        "clearable": false,
        "visibleToHarvest": false,
        "persistsWhenEmpty": true,
        "farmable": false,
        "smoothPlacement": true,
        "clearConsumption": "all",
        "requiresGrowthTerrain": false,
        "requiresPermanentDepositsTerrain": true
      },
      "yields": {
        "stone": {
          "capacity": 5,
          "initial": 1,
          "seedReserve": 1,
          "growthRate": 196608,
          "consumption": "infinite",
          "destroysDeposit": false,
          "placementMaximum": 4
        }
      },
      "requiredExperiment": "",
      "presentation": {
        "name": "Rocks",
        "sprite": "data/gfx/ressource",
        "minimap": [
          104,
          112,
          124
        ],
        "levels": [
          {
            "stock": 0,
            "variants": [
              {
                "frame": 30,
                "weight": 1
              },
              {
                "frame": 35,
                "weight": 1
              }
            ]
          },
          {
            "stock": 1,
            "variants": [
              {
                "frame": 31,
                "weight": 1
              },
              {
                "frame": 36,
                "weight": 1
              }
            ]
          },
          {
            "stock": 2,
            "variants": [
              {
                "frame": 32,
                "weight": 1
              },
              {
                "frame": 37,
                "weight": 1
              }
            ]
          },
          {
            "stock": 3,
            "variants": [
              {
                "frame": 33,
                "weight": 1
              },
              {
                "frame": 38,
                "weight": 1
              }
            ]
          },
          {
            "stock": 4,
            "variants": [
              {
                "frame": 34,
                "weight": 1
              },
              {
                "frame": 39,
                "weight": 1
              }
            ]
          },
          {
            "stock": 5,
            "variants": [
              {
                "frame": 35,
                "weight": 1
              },
              {
                "frame": 40,
                "weight": 1
              }
            ]
          }
        ],
        "animationFrames": 1,
        "animationTicks": 1,
        "animationStride": 1
      }
    },
    {
      "key": "algae",
      "properties": {
        "primaryMaterial": "algae",
        "growthRate": 196608,
        "spreadRate": 196608,
        "ecology": "shore",
        "habitatMask": 2,
        "stockBranchDivisor": 8,
        "stockDependentGrowth": true,
        "blocksGround": true,
        "blocksAir": false,
        "blocksBuilding": true,
        "clearable": true,
        "visibleToHarvest": false,
        "persistsWhenEmpty": false,
        "farmable": true,
        "smoothPlacement": true,
        "clearConsumption": "one",
        "requiresGrowthTerrain": true,
        "requiresPermanentDepositsTerrain": false
      },
      "yields": {
        "algae": {
          "capacity": 5,
          "initial": 1,
          "seedReserve": 1,
          "growthRate": 196608,
          "consumption": "one",
          "destroysDeposit": false,
          "placementMaximum": 4
        }
      },
      "requiredExperiment": "",
      "presentation": {
        "name": "Algae",
        "sprite": "data/gfx/ressource",
        "minimap": [
          41,
          157,
          165
        ],
        "levels": [
          {
            "stock": 0,
            "variants": [
              {
                "frame": 40,
                "weight": 1
              },
              {
                "frame": 45,
                "weight": 1
              }
            ]
          },
          {
            "stock": 1,
            "variants": [
              {
                "frame": 40,
                "weight": 1
              },
              {
                "frame": 45,
                "weight": 1
              }
            ]
          },
          {
            "stock": 2,
            "variants": [
              {
                "frame": 41,
                "weight": 1
              },
              {
                "frame": 46,
                "weight": 1
              }
            ]
          },
          {
            "stock": 3,
            "variants": [
              {
                "frame": 42,
                "weight": 1
              },
              {
                "frame": 47,
                "weight": 1
              }
            ]
          },
          {
            "stock": 4,
            "variants": [
              {
                "frame": 43,
                "weight": 1
              },
              {
                "frame": 48,
                "weight": 1
              }
            ]
          },
          {
            "stock": 5,
            "variants": [
              {
                "frame": 44,
                "weight": 1
              },
              {
                "frame": 49,
                "weight": 1
              }
            ]
          }
        ],
        "animationFrames": 1,
        "animationTicks": 1,
        "animationStride": 1
      }
    },
    {
      "key": "cherry-tree",
      "properties": {
        "primaryMaterial": "cherries",
        "growthRate": 196608,
        "spreadRate": 0,
        "ecology": "uniform",
        "habitatMask": 1,
        "stockBranchDivisor": 8,
        "stockDependentGrowth": true,
        "blocksGround": true,
        "blocksAir": false,
        "blocksBuilding": true,
        "clearable": false,
        "visibleToHarvest": true,
        "persistsWhenEmpty": true,
        "farmable": false,
        "smoothPlacement": false,
        "clearConsumption": "all",
        "requiresGrowthTerrain": true,
        "requiresPermanentDepositsTerrain": false
      },
      "yields": {
        "cherries": {
          "capacity": 4,
          "initial": 1,
          "seedReserve": 1,
          "growthRate": 196608,
          "consumption": "one",
          "destroysDeposit": false,
          "placementMaximum": 3
        }
      },
      "requiredExperiment": "",
      "presentation": {
        "name": "Cherry tree",
        "sprite": "data/gfx/ressource",
        "minimap": [
          255,
          127,
          0
        ],
        "levels": [
          {
            "stock": 0,
            "variants": [
              {
                "frame": 50,
                "weight": 1
              }
            ]
          },
          {
            "stock": 1,
            "variants": [
              {
                "frame": 51,
                "weight": 1
              }
            ]
          },
          {
            "stock": 2,
            "variants": [
              {
                "frame": 52,
                "weight": 1
              }
            ]
          },
          {
            "stock": 3,
            "variants": [
              {
                "frame": 53,
                "weight": 1
              }
            ]
          },
          {
            "stock": 4,
            "variants": [
              {
                "frame": 54,
                "weight": 1
              }
            ]
          }
        ],
        "animationFrames": 1,
        "animationTicks": 1,
        "animationStride": 1
      }
    },
    {
      "key": "orange-tree",
      "properties": {
        "primaryMaterial": "oranges",
        "growthRate": 196608,
        "spreadRate": 0,
        "ecology": "uniform",
        "habitatMask": 1,
        "stockBranchDivisor": 8,
        "stockDependentGrowth": true,
        "blocksGround": true,
        "blocksAir": false,
        "blocksBuilding": true,
        "clearable": false,
        "visibleToHarvest": true,
        "persistsWhenEmpty": true,
        "farmable": false,
        "smoothPlacement": false,
        "clearConsumption": "all",
        "requiresGrowthTerrain": true,
        "requiresPermanentDepositsTerrain": false
      },
      "yields": {
        "oranges": {
          "capacity": 4,
          "initial": 1,
          "seedReserve": 1,
          "growthRate": 196608,
          "consumption": "one",
          "destroysDeposit": false,
          "placementMaximum": 3
        }
      },
      "requiredExperiment": "",
      "presentation": {
        "name": "Orange tree",
        "sprite": "data/gfx/ressource",
        "minimap": [
          255,
          127,
          0
        ],
        "levels": [
          {
            "stock": 0,
            "variants": [
              {
                "frame": 55,
                "weight": 1
              }
            ]
          },
          {
            "stock": 1,
            "variants": [
              {
                "frame": 56,
                "weight": 1
              }
            ]
          },
          {
            "stock": 2,
            "variants": [
              {
                "frame": 57,
                "weight": 1
              }
            ]
          },
          {
            "stock": 3,
            "variants": [
              {
                "frame": 58,
                "weight": 1
              }
            ]
          },
          {
            "stock": 4,
            "variants": [
              {
                "frame": 59,
                "weight": 1
              }
            ]
          }
        ],
        "animationFrames": 1,
        "animationTicks": 1,
        "animationStride": 1
      }
    },
    {
      "key": "prune-tree",
      "properties": {
        "primaryMaterial": "prunes",
        "growthRate": 196608,
        "spreadRate": 0,
        "ecology": "uniform",
        "habitatMask": 1,
        "stockBranchDivisor": 8,
        "stockDependentGrowth": true,
        "blocksGround": true,
        "blocksAir": false,
        "blocksBuilding": true,
        "clearable": false,
        "visibleToHarvest": true,
        "persistsWhenEmpty": true,
        "farmable": false,
        "smoothPlacement": false,
        "clearConsumption": "all",
        "requiresGrowthTerrain": true,
        "requiresPermanentDepositsTerrain": false
      },
      "yields": {
        "prunes": {
          "capacity": 4,
          "initial": 1,
          "seedReserve": 1,
          "growthRate": 196608,
          "consumption": "one",
          "destroysDeposit": false,
          "placementMaximum": 3
        }
      },
      "requiredExperiment": "",
      "presentation": {
        "name": "Prune tree",
        "sprite": "data/gfx/ressource",
        "minimap": [
          255,
          127,
          0
        ],
        "levels": [
          {
            "stock": 0,
            "variants": [
              {
                "frame": 60,
                "weight": 1
              }
            ]
          },
          {
            "stock": 1,
            "variants": [
              {
                "frame": 61,
                "weight": 1
              }
            ]
          },
          {
            "stock": 2,
            "variants": [
              {
                "frame": 62,
                "weight": 1
              }
            ]
          },
          {
            "stock": 3,
            "variants": [
              {
                "frame": 63,
                "weight": 1
              }
            ]
          },
          {
            "stock": 4,
            "variants": [
              {
                "frame": 64,
                "weight": 1
              }
            ]
          }
        ],
        "animationFrames": 1,
        "animationTicks": 1,
        "animationStride": 1
      }
    }
  ]
}
)legacy_resources"
