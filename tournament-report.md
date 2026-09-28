# Six-policy wheat maturity follow-up

Analyzed 2304 games: 1152 new candidate games and 1152 accepted matched controls, on 192 maps across 16 families. This is a follow-up on maps used in policy selection, not an independent holdout.

The combined policy retains boundary-grid seeds and expansion protection and adds temporary protection to other eligible wheat stacks of 1–3 units. It releases temporary protection at 4–5 or empty on farming updates. The diagonal candidate protects only one checkerboard diagonal until four units, retaining expansion obligations but no permanent interior seeds. The layout-maturity candidate replaces permanent dotted-interior/checkerboard-boundary seed protection with maturity protection while retaining empty targets and supplemental support. No production source was changed.

Effects are paired geometric changes, averaged across both rotations per map. Families and opponent groups are equally weighted. Primary intervals use 50,000 stratified map-block bootstrap draws and Bonferroni correction for twelve comparisons × two endpoints (99.792% per interval).

| Comparison | Total harvest | Adjusted interval | Later harvest | Adjusted interval |
|---|---:|---:|---:|---:|
| boundary_mature4_vs_baseline | -9.1% | -20.8% to +4.5% | -2.9% | -38.3% to +54.2% |
| boundary_mature4_vs_boundary_grid | -17.1% | -27.8% to -4.1% | -4.2% | -38.0% to +49.2% |
| boundary_mature4_vs_mature4 | -27.1% | -36.3% to -16.6% | -3.8% | -39.6% to +52.0% |
| diagonal_mature4_vs_baseline | +25.1% | +11.0% to +40.4% | +28.1% | -16.3% to +94.1% |
| diagonal_mature4_vs_boundary_grid | +14.2% | +1.5% to +28.3% | +26.3% | -16.0% to +89.3% |
| diagonal_mature4_vs_mature4 | +0.3% | -12.2% to +15.0% | +26.9% | -19.6% to +100.1% |
| layout_mature4_vs_baseline | +27.1% | +12.0% to +44.1% | +3.6% | -34.8% to +63.4% |
| layout_mature4_vs_boundary_grid | +16.0% | +2.4% to +31.5% | +2.2% | -34.3% to +58.9% |
| layout_mature4_vs_mature4 | +1.9% | -10.0% to +16.0% | +2.6% | -31.8% to +56.8% |
| diagonal_mature4_vs_boundary_mature4 | +37.7% | +19.3% to +58.7% | +31.9% | -18.4% to +112.4% |
| layout_mature4_vs_boundary_mature4 | +39.9% | +22.1% to +60.6% | +6.7% | -34.5% to +73.0% |
| layout_mature4_vs_diagonal_mature4 | +1.6% | -10.0% to +14.9% | -19.1% | -47.1% to +22.4% |

## Feeding, growth, and equal-duration sensitivity

Secondary intervals are exploratory 95% intervals. Stock and natural-growth measurements cover a fixed footprint around the initial buildings, not every later expansion. Positive harvest alone does not establish sustainability.

| Comparison | Metric | Effect | 95% interval |
|---|---|---:|---:|
| boundary_mature4_vs_baseline | meals | -19.082% | -28.201 to -8.846% |
| boundary_mature4_vs_baseline | delivered | -9.616% | -17.112 to -1.408% |
| boundary_mature4_vs_baseline | starved | -9.359 | -22.909 to +4.005 |
| boundary_mature4_vs_baseline | fixed_net | +393.768 | +327.167 to +463.183 |
| boundary_mature4_vs_baseline | fixed_stock | +138.633 | +119.573 to +157.979 |
| boundary_mature4_vs_baseline | fixed_tiles | +39.432 | +34.914 to +43.974 |
| boundary_mature4_vs_baseline | blocked_fraction | +0.008 | +0.002 to +0.014 |
| boundary_mature4_vs_baseline | critical_fraction | -0.001 | -0.005 to +0.002 |
| boundary_mature4_vs_baseline | unserved_fraction | -0.002 | -0.007 to +0.002 |
| boundary_mature4_vs_baseline | mean_population | -14.947 | -21.473 to -8.334 |
| boundary_mature4_vs_baseline | starvation_per_1000_unit_minutes | +0.711 | -0.572 to +1.994 |
| boundary_mature4_vs_baseline | won | -0.052 | -0.096 to -0.008 |
| boundary_mature4_vs_baseline | late_fixed_stock | -3.263 | -18.797 to +12.518 |
| boundary_mature4_vs_baseline | late_fixed_tiles | -2.078 | -5.685 to +1.568 |
| boundary_mature4_vs_baseline | late_fixed_net | +205.182 | +144.838 to +268.747 |
| boundary_mature4_vs_baseline | common_harvest | -10.396% | -13.905 to -6.723% |
| boundary_mature4_vs_baseline | common_late_harvest | -4.036% | -11.257 to +3.535% |
| boundary_mature4_vs_boundary_grid | meals | -23.555% | -32.277 to -13.572% |
| boundary_mature4_vs_boundary_grid | delivered | -17.561% | -24.621 to -9.655% |
| boundary_mature4_vs_boundary_grid | starved | -40.169 | -56.440 to -23.883 |
| boundary_mature4_vs_boundary_grid | fixed_net | +325.745 | +248.518 to +403.315 |
| boundary_mature4_vs_boundary_grid | fixed_stock | +189.555 | +171.344 to +208.279 |
| boundary_mature4_vs_boundary_grid | fixed_tiles | +50.323 | +45.943 to +54.768 |
| boundary_mature4_vs_boundary_grid | blocked_fraction | +0.013 | +0.008 to +0.019 |
| boundary_mature4_vs_boundary_grid | critical_fraction | -0.002 | -0.006 to +0.002 |
| boundary_mature4_vs_boundary_grid | unserved_fraction | -0.004 | -0.010 to +0.001 |
| boundary_mature4_vs_boundary_grid | mean_population | -25.271 | -32.628 to -17.682 |
| boundary_mature4_vs_boundary_grid | starvation_per_1000_unit_minutes | -0.333 | -1.790 to +1.129 |
| boundary_mature4_vs_boundary_grid | won | -0.086 | -0.135 to -0.036 |
| boundary_mature4_vs_boundary_grid | late_fixed_stock | +12.836 | -3.370 to +29.620 |
| boundary_mature4_vs_boundary_grid | late_fixed_tiles | +1.516 | -2.216 to +5.344 |
| boundary_mature4_vs_boundary_grid | late_fixed_net | +148.833 | +77.932 to +220.086 |
| boundary_mature4_vs_boundary_grid | common_harvest | -19.530% | -22.822 to -16.013% |
| boundary_mature4_vs_boundary_grid | common_late_harvest | -13.787% | -20.745 to -6.384% |
| boundary_mature4_vs_mature4 | meals | -27.453% | -35.381 to -18.482% |
| boundary_mature4_vs_mature4 | delivered | -27.400% | -33.287 to -20.992% |
| boundary_mature4_vs_mature4 | starved | -47.568 | -66.833 to -28.565 |
| boundary_mature4_vs_mature4 | fixed_net | +222.784 | +146.619 to +300.229 |
| boundary_mature4_vs_mature4 | fixed_stock | +254.643 | +227.831 to +281.651 |
| boundary_mature4_vs_mature4 | fixed_tiles | +59.008 | +52.797 to +65.250 |
| boundary_mature4_vs_mature4 | blocked_fraction | +0.003 | -0.004 to +0.010 |
| boundary_mature4_vs_mature4 | critical_fraction | -0.003 | -0.008 to +0.001 |
| boundary_mature4_vs_mature4 | unserved_fraction | -0.007 | -0.013 to -0.002 |
| boundary_mature4_vs_mature4 | mean_population | -32.611 | -40.277 to -25.051 |
| boundary_mature4_vs_mature4 | starvation_per_1000_unit_minutes | -0.908 | -2.552 to +0.705 |
| boundary_mature4_vs_mature4 | won | -0.125 | -0.167 to -0.083 |
| boundary_mature4_vs_mature4 | late_fixed_stock | +92.544 | +72.750 to +112.471 |
| boundary_mature4_vs_mature4 | late_fixed_tiles | +22.854 | +18.326 to +27.370 |
| boundary_mature4_vs_mature4 | late_fixed_net | +161.841 | +93.687 to +232.016 |
| boundary_mature4_vs_mature4 | common_harvest | -30.311% | -32.825 to -27.703% |
| boundary_mature4_vs_mature4 | common_late_harvest | -22.368% | -28.682 to -15.985% |
| diagonal_mature4_vs_baseline | meals | +15.357% | +3.728 to +28.220% |
| diagonal_mature4_vs_baseline | delivered | +24.978% | +15.645 to +34.978% |
| diagonal_mature4_vs_baseline | starved | +30.828 | +13.107 to +49.149 |
| diagonal_mature4_vs_baseline | fixed_net | +223.299 | +154.060 to +295.198 |
| diagonal_mature4_vs_baseline | fixed_stock | -54.474 | -70.214 to -38.646 |
| diagonal_mature4_vs_baseline | fixed_tiles | -7.432 | -11.062 to -3.807 |
| diagonal_mature4_vs_baseline | blocked_fraction | +0.002 | -0.002 to +0.007 |
| diagonal_mature4_vs_baseline | critical_fraction | +0.001 | -0.003 to +0.005 |
| diagonal_mature4_vs_baseline | unserved_fraction | +0.005 | -0.000 to +0.010 |
| diagonal_mature4_vs_baseline | mean_population | +19.121 | +12.194 to +26.055 |
| diagonal_mature4_vs_baseline | starvation_per_1000_unit_minutes | +0.824 | -0.373 to +2.030 |
| diagonal_mature4_vs_baseline | won | +0.073 | +0.031 to +0.115 |
| diagonal_mature4_vs_baseline | late_fixed_stock | -27.586 | -40.763 to -13.669 |
| diagonal_mature4_vs_baseline | late_fixed_tiles | -6.966 | -10.086 to -3.701 |
| diagonal_mature4_vs_baseline | late_fixed_net | +142.750 | +78.690 to +209.591 |
| diagonal_mature4_vs_baseline | common_harvest | +24.252% | +20.290 to +28.337% |
| diagonal_mature4_vs_baseline | common_late_harvest | +24.638% | +18.016 to +31.781% |
| diagonal_mature4_vs_boundary_grid | meals | +8.980% | -1.428 to +20.336% |
| diagonal_mature4_vs_boundary_grid | delivered | +13.992% | +5.783 to +22.808% |
| diagonal_mature4_vs_boundary_grid | starved | +0.018 | -19.774 to +20.148 |
| diagonal_mature4_vs_boundary_grid | fixed_net | +155.276 | +87.416 to +225.659 |
| diagonal_mature4_vs_boundary_grid | fixed_stock | -3.552 | -20.557 to +13.810 |
| diagonal_mature4_vs_boundary_grid | fixed_tiles | +3.458 | -0.500 to +7.474 |
| diagonal_mature4_vs_boundary_grid | blocked_fraction | +0.008 | +0.003 to +0.012 |
| diagonal_mature4_vs_boundary_grid | critical_fraction | +0.001 | -0.003 to +0.005 |
| diagonal_mature4_vs_boundary_grid | unserved_fraction | +0.003 | -0.003 to +0.008 |
| diagonal_mature4_vs_boundary_grid | mean_population | +8.798 | +1.398 to +16.160 |
| diagonal_mature4_vs_boundary_grid | starvation_per_1000_unit_minutes | -0.220 | -1.556 to +1.113 |
| diagonal_mature4_vs_boundary_grid | won | +0.039 | -0.003 to +0.081 |
| diagonal_mature4_vs_boundary_grid | late_fixed_stock | -11.487 | -25.969 to +3.456 |
| diagonal_mature4_vs_boundary_grid | late_fixed_tiles | -3.372 | -6.721 to +0.060 |
| diagonal_mature4_vs_boundary_grid | late_fixed_net | +86.401 | +23.521 to +151.115 |
| diagonal_mature4_vs_boundary_grid | common_harvest | +11.585% | +8.241 to +15.099% |
| diagonal_mature4_vs_boundary_grid | common_late_harvest | +11.974% | +5.883 to +18.628% |
| diagonal_mature4_vs_mature4 | meals | +3.423% | -7.374 to +15.556% |
| diagonal_mature4_vs_mature4 | delivered | +0.388% | -7.950 to +9.419% |
| diagonal_mature4_vs_mature4 | starved | -7.380 | -29.526 to +14.511 |
| diagonal_mature4_vs_mature4 | fixed_net | +52.315 | -20.336 to +128.714 |
| diagonal_mature4_vs_mature4 | fixed_stock | +61.536 | +39.349 to +83.563 |
| diagonal_mature4_vs_mature4 | fixed_tiles | +12.143 | +6.862 to +17.424 |
| diagonal_mature4_vs_mature4 | blocked_fraction | -0.003 | -0.009 to +0.004 |
| diagonal_mature4_vs_mature4 | critical_fraction | -0.001 | -0.005 to +0.004 |
| diagonal_mature4_vs_mature4 | unserved_fraction | -0.000 | -0.007 to +0.006 |
| diagonal_mature4_vs_mature4 | mean_population | +1.458 | -7.064 to +10.011 |
| diagonal_mature4_vs_mature4 | starvation_per_1000_unit_minutes | -0.795 | -2.416 to +0.734 |
| diagonal_mature4_vs_mature4 | won | +0.000 | -0.042 to +0.042 |
| diagonal_mature4_vs_mature4 | late_fixed_stock | +68.221 | +49.919 to +86.537 |
| diagonal_mature4_vs_mature4 | late_fixed_tiles | +17.966 | +13.583 to +22.391 |
| diagonal_mature4_vs_mature4 | late_fixed_net | +99.409 | +34.927 to +167.277 |
| diagonal_mature4_vs_mature4 | common_harvest | -3.363% | -7.041 to +0.470% |
| diagonal_mature4_vs_mature4 | common_late_harvest | +0.828% | -4.704 to +6.794% |
| layout_mature4_vs_baseline | meals | +19.786% | +7.680 to +33.247% |
| layout_mature4_vs_baseline | delivered | +27.120% | +17.297 to +37.713% |
| layout_mature4_vs_baseline | starved | +63.195 | +41.193 to +85.727 |
| layout_mature4_vs_baseline | fixed_net | -35.497 | -100.151 to +28.065 |
| layout_mature4_vs_baseline | fixed_stock | -200.896 | -220.932 to -181.281 |
| layout_mature4_vs_baseline | fixed_tiles | -43.932 | -48.635 to -39.302 |
| layout_mature4_vs_baseline | blocked_fraction | -0.020 | -0.025 to -0.016 |
| layout_mature4_vs_baseline | critical_fraction | +0.006 | +0.002 to +0.010 |
| layout_mature4_vs_baseline | unserved_fraction | +0.013 | +0.007 to +0.018 |
| layout_mature4_vs_baseline | mean_population | +23.493 | +16.113 to +31.080 |
| layout_mature4_vs_baseline | starvation_per_1000_unit_minutes | +3.135 | +1.774 to +4.514 |
| layout_mature4_vs_baseline | won | +0.068 | +0.023 to +0.112 |
| layout_mature4_vs_baseline | late_fixed_stock | -73.490 | -87.932 to -59.479 |
| layout_mature4_vs_baseline | late_fixed_tiles | -18.549 | -21.974 to -15.234 |
| layout_mature4_vs_baseline | late_fixed_net | -52.727 | -111.381 to +4.904 |
| layout_mature4_vs_baseline | common_harvest | +33.556% | +28.899 to +38.344% |
| layout_mature4_vs_baseline | common_late_harvest | +22.424% | +15.050 to +30.474% |
| layout_mature4_vs_boundary_grid | meals | +13.164% | +2.375 to +25.183% |
| layout_mature4_vs_boundary_grid | delivered | +15.946% | +7.281 to +25.492% |
| layout_mature4_vs_boundary_grid | starved | +32.385 | +9.695 to +55.240 |
| layout_mature4_vs_boundary_grid | fixed_net | -103.521 | -171.060 to -34.776 |
| layout_mature4_vs_boundary_grid | fixed_stock | -149.974 | -170.466 to -129.344 |
| layout_mature4_vs_boundary_grid | fixed_tiles | -33.042 | -37.870 to -28.201 |
| layout_mature4_vs_boundary_grid | blocked_fraction | -0.015 | -0.020 to -0.010 |
| layout_mature4_vs_boundary_grid | critical_fraction | +0.006 | +0.002 to +0.010 |
| layout_mature4_vs_boundary_grid | unserved_fraction | +0.011 | +0.005 to +0.016 |
| layout_mature4_vs_boundary_grid | mean_population | +13.170 | +5.392 to +20.922 |
| layout_mature4_vs_boundary_grid | starvation_per_1000_unit_minutes | +2.090 | +0.599 to +3.562 |
| layout_mature4_vs_boundary_grid | won | +0.034 | -0.010 to +0.078 |
| layout_mature4_vs_boundary_grid | late_fixed_stock | -57.391 | -72.016 to -42.476 |
| layout_mature4_vs_boundary_grid | late_fixed_tiles | -14.956 | -18.412 to -11.469 |
| layout_mature4_vs_boundary_grid | late_fixed_net | -109.076 | -169.391 to -48.031 |
| layout_mature4_vs_boundary_grid | common_harvest | +19.940% | +15.797 to +24.265% |
| layout_mature4_vs_boundary_grid | common_late_harvest | +9.984% | +3.666 to +16.757% |
| layout_mature4_vs_mature4 | meals | +7.394% | -3.761 to +20.081% |
| layout_mature4_vs_mature4 | delivered | +2.108% | -5.890 to +10.837% |
| layout_mature4_vs_mature4 | starved | +24.987 | -1.297 to +52.151 |
| layout_mature4_vs_mature4 | fixed_net | -206.482 | -258.797 to -154.549 |
| layout_mature4_vs_mature4 | fixed_stock | -84.885 | -107.182 to -62.627 |
| layout_mature4_vs_mature4 | fixed_tiles | -24.357 | -29.690 to -19.013 |
| layout_mature4_vs_mature4 | blocked_fraction | -0.025 | -0.031 to -0.019 |
| layout_mature4_vs_mature4 | critical_fraction | +0.005 | +0.000 to +0.009 |
| layout_mature4_vs_mature4 | unserved_fraction | +0.008 | +0.002 to +0.013 |
| layout_mature4_vs_mature4 | mean_population | +5.829 | -2.536 to +14.126 |
| layout_mature4_vs_mature4 | starvation_per_1000_unit_minutes | +1.515 | -0.048 to +3.038 |
| layout_mature4_vs_mature4 | won | -0.005 | -0.049 to +0.039 |
| layout_mature4_vs_mature4 | late_fixed_stock | +22.318 | +4.828 to +39.526 |
| layout_mature4_vs_mature4 | late_fixed_tiles | +6.383 | +2.206 to +10.505 |
| layout_mature4_vs_mature4 | late_fixed_net | -96.068 | -143.068 to -49.129 |
| layout_mature4_vs_mature4 | common_harvest | +3.873% | +0.176 to +7.740% |
| layout_mature4_vs_mature4 | common_late_harvest | -0.963% | -6.099 to +4.573% |
| diagonal_mature4_vs_boundary_mature4 | meals | +42.560% | +26.682 to +60.496% |
| diagonal_mature4_vs_boundary_mature4 | delivered | +38.275% | +26.185 to +51.612% |
| diagonal_mature4_vs_boundary_mature4 | starved | +40.188 | +22.825 to +58.013 |
| diagonal_mature4_vs_boundary_mature4 | fixed_net | -170.469 | -242.703 to -98.294 |
| diagonal_mature4_vs_boundary_mature4 | fixed_stock | -193.107 | -211.599 to -174.341 |
| diagonal_mature4_vs_boundary_mature4 | fixed_tiles | -46.865 | -51.112 to -42.539 |
| diagonal_mature4_vs_boundary_mature4 | blocked_fraction | -0.006 | -0.012 to +0.001 |
| diagonal_mature4_vs_boundary_mature4 | critical_fraction | +0.003 | -0.002 to +0.007 |
| diagonal_mature4_vs_boundary_mature4 | unserved_fraction | +0.007 | +0.001 to +0.013 |
| diagonal_mature4_vs_boundary_mature4 | mean_population | +34.068 | +26.191 to +41.884 |
| diagonal_mature4_vs_boundary_mature4 | starvation_per_1000_unit_minutes | +0.113 | -1.308 to +1.547 |
| diagonal_mature4_vs_boundary_mature4 | won | +0.125 | +0.081 to +0.169 |
| diagonal_mature4_vs_boundary_mature4 | late_fixed_stock | -24.323 | -39.385 to -9.388 |
| diagonal_mature4_vs_boundary_mature4 | late_fixed_tiles | -4.888 | -8.177 to -1.604 |
| diagonal_mature4_vs_boundary_mature4 | late_fixed_net | -62.432 | -126.768 to +2.164 |
| diagonal_mature4_vs_boundary_mature4 | common_harvest | +38.668% | +32.753 to +44.915% |
| diagonal_mature4_vs_boundary_mature4 | common_late_harvest | +29.879% | +20.227 to +40.845% |
| layout_mature4_vs_boundary_mature4 | meals | +48.034% | +31.704 to +66.278% |
| layout_mature4_vs_boundary_mature4 | delivered | +40.644% | +28.771 to +53.438% |
| layout_mature4_vs_boundary_mature4 | starved | +72.555 | +51.333 to +94.755 |
| layout_mature4_vs_boundary_mature4 | fixed_net | -429.266 | -504.310 to -356.003 |
| layout_mature4_vs_boundary_mature4 | fixed_stock | -339.529 | -363.924 to -315.833 |
| layout_mature4_vs_boundary_mature4 | fixed_tiles | -83.365 | -89.016 to -77.812 |
| layout_mature4_vs_boundary_mature4 | blocked_fraction | -0.029 | -0.035 to -0.022 |
| layout_mature4_vs_boundary_mature4 | critical_fraction | +0.008 | +0.003 to +0.012 |
| layout_mature4_vs_boundary_mature4 | unserved_fraction | +0.015 | +0.009 to +0.021 |
| layout_mature4_vs_boundary_mature4 | mean_population | +38.440 | +30.627 to +46.358 |
| layout_mature4_vs_boundary_mature4 | starvation_per_1000_unit_minutes | +2.423 | +0.804 to +4.051 |
| layout_mature4_vs_boundary_mature4 | won | +0.120 | +0.073 to +0.167 |
| layout_mature4_vs_boundary_mature4 | late_fixed_stock | -70.227 | -87.810 to -53.792 |
| layout_mature4_vs_boundary_mature4 | late_fixed_tiles | -16.471 | -20.427 to -12.758 |
| layout_mature4_vs_boundary_mature4 | late_fixed_net | -257.909 | -324.836 to -192.779 |
| layout_mature4_vs_boundary_mature4 | common_harvest | +49.051% | +42.886 to +55.342% |
| layout_mature4_vs_boundary_mature4 | common_late_harvest | +27.572% | +17.177 to +39.686% |
| layout_mature4_vs_diagonal_mature4 | meals | +3.839% | -6.263 to +15.225% |
| layout_mature4_vs_diagonal_mature4 | delivered | +1.714% | -5.743 to +9.971% |
| layout_mature4_vs_diagonal_mature4 | starved | +32.367 | +10.154 to +55.544 |
| layout_mature4_vs_diagonal_mature4 | fixed_net | -258.797 | -330.368 to -190.435 |
| layout_mature4_vs_diagonal_mature4 | fixed_stock | -146.422 | -163.021 to -130.200 |
| layout_mature4_vs_diagonal_mature4 | fixed_tiles | -36.500 | -40.443 to -32.641 |
| layout_mature4_vs_diagonal_mature4 | blocked_fraction | -0.023 | -0.027 to -0.018 |
| layout_mature4_vs_diagonal_mature4 | critical_fraction | +0.005 | +0.001 to +0.009 |
| layout_mature4_vs_diagonal_mature4 | unserved_fraction | +0.008 | +0.003 to +0.013 |
| layout_mature4_vs_diagonal_mature4 | mean_population | +4.372 | -2.783 to +11.753 |
| layout_mature4_vs_diagonal_mature4 | starvation_per_1000_unit_minutes | +2.310 | +0.963 to +3.685 |
| layout_mature4_vs_diagonal_mature4 | won | -0.005 | -0.047 to +0.039 |
| layout_mature4_vs_diagonal_mature4 | late_fixed_stock | -45.904 | -58.641 to -33.445 |
| layout_mature4_vs_diagonal_mature4 | late_fixed_tiles | -11.583 | -14.643 to -8.596 |
| layout_mature4_vs_diagonal_mature4 | late_fixed_net | -195.477 | -262.997 to -131.232 |
| layout_mature4_vs_diagonal_mature4 | common_harvest | +7.488% | +4.012 to +11.002% |
| layout_mature4_vs_diagonal_mature4 | common_late_harvest | -1.776% | -6.591 to +3.464% |

## Map sizes

| Comparison | Size | Maps | Harvest | Later harvest | Meals |
|---|---:|---:|---:|---:|---:|
| boundary_mature4_vs_baseline | 128 | 150 | -10.0% | +1.1% | -20.7% |
| boundary_mature4_vs_baseline | 256 | 42 | -6.2% | -15.9% | -13.2% |
| boundary_mature4_vs_boundary_grid | 128 | 150 | -15.8% | +8.1% | -22.2% |
| boundary_mature4_vs_boundary_grid | 256 | 42 | -21.3% | -37.8% | -28.1% |
| boundary_mature4_vs_mature4 | 128 | 150 | -29.0% | -3.6% | -30.0% |
| boundary_mature4_vs_mature4 | 256 | 42 | -20.3% | -4.2% | -17.7% |
| diagonal_mature4_vs_baseline | 128 | 150 | +21.6% | +20.6% | +10.4% |
| diagonal_mature4_vs_baseline | 256 | 42 | +38.5% | +58.8% | +34.9% |
| diagonal_mature4_vs_boundary_grid | 128 | 150 | +13.7% | +29.0% | +8.2% |
| diagonal_mature4_vs_boundary_grid | 256 | 42 | +16.2% | +17.4% | +11.7% |
| diagonal_mature4_vs_mature4 | 128 | 150 | -4.1% | +15.0% | -2.6% |
| diagonal_mature4_vs_mature4 | 256 | 42 | +17.7% | +80.8% | +28.0% |
| layout_mature4_vs_baseline | 128 | 150 | +22.9% | -5.9% | +14.2% |
| layout_mature4_vs_baseline | 256 | 42 | +43.3% | +45.6% | +42.2% |
| layout_mature4_vs_boundary_grid | 128 | 150 | +14.9% | +0.7% | +11.9% |
| layout_mature4_vs_boundary_grid | 256 | 42 | +20.3% | +7.6% | +17.8% |
| layout_mature4_vs_mature4 | 128 | 150 | -3.0% | -10.2% | +0.7% |
| layout_mature4_vs_mature4 | 256 | 42 | +21.8% | +65.7% | +35.0% |
| diagonal_mature4_vs_boundary_mature4 | 128 | 150 | +35.0% | +19.3% | +39.2% |
| diagonal_mature4_vs_boundary_mature4 | 256 | 42 | +47.6% | +88.8% | +55.4% |
| layout_mature4_vs_boundary_mature4 | 128 | 150 | +36.5% | -6.9% | +43.9% |
| layout_mature4_vs_boundary_mature4 | 256 | 42 | +52.7% | +73.0% | +63.9% |
| layout_mature4_vs_diagonal_mature4 | 128 | 150 | +1.1% | -21.9% | +3.4% |
| layout_mature4_vs_diagonal_mature4 | 256 | 42 | +3.5% | -8.3% | +5.5% |

## Duration and provenance

1905 of 2304 games ended before the 98,304-tick limit. Equal-duration comparisons truncate all six policies in each matched start to their last common logged tick. This can leave little or no late-game exposure; do not infer sustained performance from an early-match gain.

Accepted outputs by host: {'localhost': 180, 'therig.local': 1229, 'devlaptop.local': 895}

protocol.json freezes scope and criteria; primary-inputs.json identifies every reused/new result. summary.json preserves per-game measurements; statistics.json includes family, opponent and size breakdowns, resource/feeding metrics and leave-one-family-out sensitivity. Raw logs, replays and final saves remain under games-six-policy/ and the referenced control result directories.

cross-platform/comparison.json and cross-platform-diagonal/comparison.json record identical per-tick checksums on Arena and Rice over 8,192 ticks on all three machines. policy-smoke/verification.json verifies that extra temporary protection activates. analysis-verification.json records the known-effect statistical fixture. These checks are limited to those cases and are not universal platform or save-continuity proof.
