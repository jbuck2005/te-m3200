# Pressure Sensor & Cable Options

A curated bill of materials for a rugged 100 psi digital (I2C) pressure transducer, the TE Connectivity M3200.

## Reading an M3200 part number

Every M3200 part number follows the same template. Each character, or group of characters, answers one ordering question (M3200 datasheet p10):

```
 M32   J   ?   -   0 0   ?   ?   ?   ?   -   ????   ?
 ───   ─   ─       ───   ─   ─   ─   ─       ────   ─
  │    │   │        │    │   │   │   │        │     └─ ⑩ Pressure type
  │    │   │        │    │   │   │   │        └─────── ⑨ Pressure range
  │    │   │        │    │   │   │   └──────────────── ⑧ Pressure port (thread)
  │    │   │        │    │   │   └──────────────────── ⑦ I2C address
  │    │   │        │    │   └──────────────────────── ⑥ Sleep mode
  │    │   │        │    └──────────────────────────── ⑤ Snubber
  │    │   │        └───────────────────────────────── ④ Fixed "00"
  │    │   └────────────────────────────────────────── ③ Connection
  │    └────────────────────────────────────────────── ② Output
  └─────────────────────────────────────────────────── ① Series
```

The six digits in the middle aren't one "configuration code". They're two fixed zeros followed by four separate option fields.

| # | Field | Codes available |
|---|-------|-----------------|
| ① | Series | `M32`: M3200 Microfused industrial pressure transducer |
| ② | Output | `J`: I2C (14-bit pressure, 11-bit temperature) |
| ③ | Connection | `D`: M12 connector · `L`: 0.5 m cable · `M`: 1 m cable |
| ④ | Fixed | `00` |
| ⑤ | Snubber | `0`: none · `1`: with snubber (G1/4 port only) |
| ⑥ | Sleep mode | `0`: non-sleep (converts continuously) · `1`: sleep (converts on request) |
| ⑦ | I2C address | `0`: 0x28 · `1`: 0x36 · `2`: 0x46 · `3`: 0x48\* · `4`: 0x51\* |
| ⑧ | Pressure port | `2`: 1/4-19 BSPP · `4`: 7/16-20 UNF male (SAE J1926-2, O-ring) · `5`: 1/4-18 NPT · `6`: 1/8-27 NPT · `B`: G1/4 JIS B2351 (NBR O-ring) · `E`: 1/4-19 BSPT · `P`: 7/16-20 UNF female (SAE J513, valve depressor) |
| ⑨ | Pressure range | psi: `100P`, `250P`, `500P`, `01KP`, `2K5P`, `05KP`, `7K5P`, `10KP` · bar: `007B` to `700B` · any range from 100 to 10,000 psi |
| ⑩ | Pressure type | `G`: gage · `C`: compound (−14.7 psi to full scale) |

\*Address codes `3` and `4` are doubtful. TE's *Interfacing to Digital Pressure Modules* guide (§1.2) names only three factory addresses: 0x28, 0x36 and 0x46. Their read headers are 0x51, 0x6D and 0x8D, so the `4` = 0x51 entry looks like the 0x28 read header copied into the address column. Nothing in the guide supports `3` (0x48) either. If you order either one, confirm the address with TE or run an I2C bus scan.

## Sensor options

Both options are the same sensor. Only the connection field (③) is different, so either one works with the same driver settings: `max_pressure: 100`, with `sleep_mode` left on (the default).

| # | Field | Option 1: M12 connector<br>`M32JD-000105-100PG` | Option 2: built-in cable<br>`M32JM-000105-100PG` |
|---|-------|-------------------------|-------------------------|
| ① | Series | `M32`: M3200 | `M32`: M3200 |
| ② | Output | `J`: I2C | `J`: I2C |
| ③ | Connection | **`D`: M12 connector** | **`M`: 1 m molded, shielded cable** (`L` for 0.5 m) |
| ④ | Fixed | `00` | `00` |
| ⑤ | Snubber | `0`: none | `0`: none |
| ⑥ | Sleep mode | `1`: sleep | `1`: sleep |
| ⑦ | I2C address | `0`: 0x28 | `0`: 0x28 |
| ⑧ | Pressure port | `5`: 1/4-18 NPT | `5`: 1/4-18 NPT |
| ⑨ | Pressure range | `100P`: 0 to 100 psi | `100P`: 0 to 100 psi |
| ⑩ | Pressure type | `G`: gage | `G`: gage |

Sleep mode means the sensor idles at 5 µA and only measures when the driver sends a request, so the driver must have `sleep_mode` on (the default). With it off, every read comes back stale and the pressure never updates.

### Option 1: M12 connector, `M32JD-000105-100PG`

Best for modular or serviceable setups. You can unplug the sensor and swap it without rewiring. It needs one of the mating cables below.

### Option 2: built-in cable, `M32JM-000105-100PG`

Best for permanent outdoor installations. The cable is molded on at the factory, so the sensor head has no connector to let moisture in, and you don't need a mating cable.

## M12 cables (for Option 1)

| Part number | Price | Style | Length | Jacket | Use |
|-------------|-------|-------|--------|--------|-----|
| `PXPTPU12FBF04ACL010PUR` | ~$44 | Straight | 1 m | PUR | Outdoor (UV and cold resistant) |
| `PXPTPU12FBF04ACL020PUR` | ~$56 | Straight | 2 m | PUR | Outdoor (UV and cold resistant) |
| `PXPPNP12RAF04ACL010PVC` | ~$47 | Right-angle | 1 m | PVC | Indoor or protected only |

All three are female, threaded, 4-pin A-coded M12 connectors with overmolded cable, and all are stocked by Mouser.
