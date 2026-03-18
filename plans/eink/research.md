# E-Ink Image Retention / Burn-In Research

**Created**: 2026-03-17
**Panel**: ED047TC1 (DKE Group, using E Ink electrophoretic film)
**Used in**: M5Paper S3, LilyGo T5 E-Paper S3 Pro

---

## Panel Datasheet Findings

The ED047TC1 datasheet (v0.1, Nov 2014, saved at `ED047TC1_datasheet.pdf`) is a preliminary spec that provides **no guidance** on:
- Image retention / sticking
- Maximum static display time
- Recommended refresh intervals
- Panel lifetime (refresh cycles)
- Reliability testing (section 10 literally says "TBD")

The datasheet describes the panel as "for mobile phone use only" (i.e., e-reader), not for always-on signage.

---

## E Ink Corporation's Official Position

From [e-ink-info.com](https://www.e-ink-info.com/about-e-ink-display-persistency), citing E Ink directly:

- E Ink has a display turned off in **2001** with the image **still visible** — demonstrating 10+ years of static image retention without degradation.
- **Static images do not cause harm to the display.** The bistable technology requires no power to maintain an image, and leaving a picture unchanged indefinitely does not deteriorate the display.
- Properly sealed displays "will not deteriorate with age" with caveats for extreme temperatures and strong electromagnetic fields.
- Once a pixel's color is set, "it will not change by itself."

**Conclusion from manufacturer**: Static content is not inherently damaging.

---

## Ghosting vs. Burn-In: The Distinction

### Ghosting (temporary, reversible)
- Faint afterimage from previous content visible after a partial refresh
- Caused by charged pigment particles not fully resetting during a screen update
- **Fully reversible** via a full refresh cycle (flash black → white → redraw)
- Accumulates with many consecutive partial refreshes
- Our code already handles this with periodic full refreshes (`DISPLAY_FULL_REDRAW_TIMEOUT_MS`)

### Image Retention (long-term, still reversible)
- Occurs when the **same static image** is displayed for hours/days
- Pigment particles "settle" into resistant positions
- Described as "stubborn ghosting" — harder to clear but **still reversible** with full refresh cycles
- Multiple sources confirm this is **not permanent damage** on modern e-ink devices

### True Burn-In (permanent, extremely rare)
- Described as "extremely rare" on e-ink displays by multiple sources
- No confirmed mechanism for permanent pixel damage from static content alone
- Physical damage (heat, UV, mechanical stress) can permanently damage the panel, but that's not electronic burn-in

---

## The Reddit/Community Reports

The 3-year Lilygo T5 user reporting "very severe burn in" likely experienced:
1. **Severe image retention** from years of the same static layout, OR
2. **Physical degradation** from environmental factors (heat, UV exposure), OR
3. A combination — prolonged static content + suboptimal refresh patterns making retention very stubborn

Without knowing their refresh strategy, it's impossible to say if this was truly irreversible or just very stubborn retention that aggressive full-refresh cycling could have cleared.

The Lilygo product page warning about "irreversible damage" from partial refreshes is likely referring to **the waveform/voltage issue**: partial refresh modes use simplified voltage waveforms that don't fully drive all particles. Over many consecutive partial refreshes without an intervening full refresh, the cumulative charge imbalance can create very stubborn retention. This is why all e-ink implementations periodically do full refreshes.

---

## Practical Recommendations for This Project

### What we should do:
1. **Keep the periodic full refresh** — our 30-second `DISPLAY_FULL_REDRAW_TIMEOUT_MS` is appropriate. This clears accumulated ghosting from partial updates.
2. **Blank screen on idle is good UX but not required for panel health** — it won't prevent damage (because there isn't damage to prevent), but it's cleaner and saves power.
3. **If implementing blank-on-idle**: do a proper full refresh to white/blank rather than just stopping updates. This ensures no static content sits on the panel during long idle periods.
4. **Consider an occasional "deep clean" refresh** — cycle black → white → black → white before redrawing content. Some e-ink implementations do this every N full refreshes to ensure thorough particle movement.

### What we should NOT worry about:
- Permanent burn-in from normal use with periodic full refreshes
- Needing to blank the screen specifically to prevent panel damage
- Pixel shifting or other OLED-style burn-in mitigations

---

## Sources

- [ED047TC1 Datasheet](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/517/C139_ED047TC1_datasheet.pdf) (DKE Group, v0.1, Nov 2014)
- [E Ink Display Persistency](https://www.e-ink-info.com/about-e-ink-display-persistency) (e-ink-info.com, citing E Ink Corp directly)
- [How to Fix E-Ink Ghosting and Burn-In](https://www.paperlessmode.com/how-to-fix-e-ink-ghosting-burn-in/) (Paperless Mode)
- [E Ink Ghosting Decoded](https://viwoods.com/blogs/paper-tablet/e-ink-ghosting-explained) (Viwoods)
- [Why Does an Electronic Paper Display Flicker?](https://www.visionect.com/blog/why-epaper-blinks/) (Visionect)
- [Epaper Holds an Image Even Without Power](https://www.visionect.com/blog/did-you-know-epaper-holds-an-image-even-without-power/) (Visionect)
