# Fiddle Design System — "The Precision Console"

> Extracted from the **Fiddle Library Manager** Stitch project
> (`projects/49448312188117323`, design system `assets/8542ead75ad94c9b86ab771b065cc106`)

---

## Creative North Star

**"The Tactical Command Center"** — a high-performance, professional engineering
environment inspired by recording consoles (SSL, Neve) and modern DAWs.
Functional Brutalism: every pixel must serve a purpose. Premium feel through
mathematical precision, disciplined tonal layering, and technical utility.

---

## Color Palette

### Mode: **Dark** &nbsp;|&nbsp; Variant: **Vibrant** &nbsp;|&nbsp; Primary: `#1e4cd5` &nbsp;|&nbsp; Saturation: 2

### Surfaces (Dark-on-Dark Nesting)

| Role                       | Token                        | Hex         |
|----------------------------|------------------------------|-------------|
| Global Floor               | `background`                 | `#09082f`   |
| Surface                    | `surface`                    | `#09082f`   |
| Surface Dim                | `surface_dim`                | `#09082f`   |
| Surface Bright             | `surface_bright`             | `#25255f`   |
| Container Lowest           | `surface_container_lowest`   | `#000000`   |
| Container Low              | `surface_container_low`      | `#0e0d38`   |
| Container                  | `surface_container`          | `#131342`   |
| Container High             | `surface_container_high`     | `#19194b`   |
| Container Highest          | `surface_container_highest`  | `#1f1f55`   |
| Surface Variant            | `surface_variant`            | `#1f1f55`   |
| Surface Tint               | `surface_tint`               | `#95a9ff`   |

### Primary

| Token                     | Hex         |
|---------------------------|-------------|
| `primary`                 | `#95a9ff`   |
| `primary_dim`             | `#3866ff`   |
| `primary_container`       | `#829bff`   |
| `primary_fixed`           | `#829bff`   |
| `primary_fixed_dim`       | `#6e8cff`   |
| `on_primary`              | `#00247e`   |
| `on_primary_container`    | `#001a63`   |
| `on_primary_fixed`        | `#000000`   |
| `on_primary_fixed_variant`| `#002279`   |
| `inverse_primary`         | `#134ee8`   |

### Secondary

| Token                     | Hex         |
|---------------------------|-------------|
| `secondary`               | `#9293ff`   |
| `secondary_dim`           | `#9293ff`   |
| `secondary_container`     | `#3938a0`   |
| `secondary_fixed`         | `#cecdff`   |
| `secondary_fixed_dim`     | `#bebeff`   |
| `on_secondary`            | `#0d0078`   |
| `on_secondary_container`  | `#cdccff`   |
| `on_secondary_fixed`      | `#25228d`   |
| `on_secondary_fixed_variant` | `#4343ab` |

### Tertiary

| Token                     | Hex         |
|---------------------------|-------------|
| `tertiary`                | `#ffa7ec`   |
| `tertiary_dim`            | `#e986d7`   |
| `tertiary_container`      | `#f993e5`   |
| `tertiary_fixed`          | `#f993e5`   |
| `tertiary_fixed_dim`      | `#e986d7`   |
| `on_tertiary`             | `#6d1865`   |
| `on_tertiary_container`   | `#610a5a`   |
| `on_tertiary_fixed`       | `#3d0038`   |
| `on_tertiary_fixed_variant` | `#6c1764` |

### Error

| Token                     | Hex         |
|---------------------------|-------------|
| `error`                   | `#ff6e84`   |
| `error_dim`               | `#d73357`   |
| `error_container`         | `#a70138`   |
| `on_error`                | `#490013`   |
| `on_error_container`      | `#ffb2b9`   |

### Neutral / On-Surface

| Token                     | Hex         |
|---------------------------|-------------|
| `on_background`           | `#e5e3ff`   |
| `on_surface`              | `#e5e3ff`   |
| `on_surface_variant`      | `#a8a7d5`   |
| `outline`                 | `#72719c`   |
| `outline_variant`         | `#44446c`   |
| `inverse_surface`         | `#fcf8ff`   |
| `inverse_on_surface`      | `#51517a`   |

---

## Typography

| Role            | Font Family        | Usage                                   |
|-----------------|--------------------|-----------------------------------------|
| **Headlines**   | **Sora**           | Library titles, category headers        |
| **Body**        | **Literata**       | Long-form text, descriptions            |
| **Labels**      | **Inter**          | Metadata, file paths, sample rates, technical data |

### Guidelines

- Tighten letter-spacing on labels by `-0.01em` for the "technical instrument" feel.
- Use `on_surface_variant` (`#a8a7d5`) for non-essential metadata.
- Never use `#FFFFFF` for body text — use `on_surface` (`#e5e3ff`) max.
- Use all-caps for `label-sm` technical data.

---

## Shape & Elevation

| Property          | Value    |
|-------------------|----------|
| Corner roundness  | `4px`    |
| Structural radius | `0px`    |
| Button max radius | `2px`    |

### Depth Model (Tonal Layering, No Shadows)

- **Docked elements**: depth via surface brightness stepping, never shadows.
- **Floating menus only**: `box-shadow: 0 10px 30px rgba(0,0,0,0.5)`.
- **Ghost Borders**: `outline_variant` at 20% opacity for high-density grids.

---

## Screens

| Screen Title                       | ID                                   |
|------------------------------------|--------------------------------------|
| Initialize New Library (Updated)   | `35a00baa34874c108f442fa982232704`   |
| Library Dashboard                  | `4447c5901f08412a91089af8731e7227`   |
| Library Editor (with Remove Action)| `d272e9a2c4db48359cf34501b770b295`   |
| Library Editor (Modal Selection)   | `8b9b494b8a94409882ab1cbd79e379c3`   |
| Library Editor (Signal Chain)      | `b00413fc0bfa455da37d26a294ea3c3a`   |
| Library Editor Modal (Signal Chain)| `80bf9c4625c24765a6742aca3421950b`   |
| Library Dashboard (Signal Chain)   | `eb75c8aefbb446398dd8a089bb77032c`   |
| PRD                                | `8f425812b48f463baa1c04278ac4f13a`   |

---

## Component Rules (Quick Reference)

- **No-Line Hierarchy**: boundaries via background shifting, not `1px` borders.
- **Buttons**: rectangular, no radius. Primary: `primary` bg. Active/toggle: 2px `secondary` bottom-border.
- **Inputs**: recessed (`surface_container_lowest` bg), `primary` caret/focus ring.
- **Library Grid**: alternating `surface_container_low` / `surface_container` rows (ledger effect).
- **Panel Headers**: 24px thin bar, `surface_container_highest`, `label-sm` all-caps.
- **Glass Overlay**: `backdrop-blur(12px)`, 60% `surface_container_highest` for floating previews.
