- DONE - we should check this PR for anything useful, namely 'New Entity Types', since it seems to extend the
  functionality and support for devices
    - https://github.com/ugomeda/home-assistant-epaper-remote/pull/2
    - first, can we make sure we save this off to a branch in our build off of main and push it, so we have it for
      posterity?
    - Please realize this is a PR from a random person and may be janky or halfassed, so we need to review for quailty
      extensively
    - if we find there's value, lets create a plan as to what to cherry pick.
- IN PROGRESS - screen burn in - is this a concern? if so, what do folks generally do to alleviate? I'm fine with blank
  screen til
  someone taps which then loads the screen. if that helps, then a timeout. we can also make a better
  homescreen/screensaver if that helps as well.
- NEEDS TESTING - Does EINK suport lvgl? does this project support adding lvgl for other/new screens?
- is our current solution for tickless sleep manually managed better than just finding/building an arduino core that
  supports CONFIG_PM_ENABLE ?
- DOCUMENTED - for the M3 specifically i see 'RTC chip sleep wake-up', that of any use to us? I also see PMS150G (power
  on/off and
  program download control) && Built-in BM8563 RTC chip (supports sleep and wake-up functions) @ communication address:
  0x51 -- any of that of interest or use? even if we need to use custom arduino or otherwise, if they offer benefits,
  let's weigh them
- CLICK, DOIN IT - Buzzer Onboard passive buzzer - so, any use for this like phones use for tactile button pushing? or
  worthless?

