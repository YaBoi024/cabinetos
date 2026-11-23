-- WHERE THE SOUND GOES (#655; MMagTech's rule from #341 and #547): the screen
-- the picture is on plays the sound; an output takes over only if it reports
-- something actually plugged in; nothing else takes the sound. When the
-- screen's sound is gone (asleep, off, unplugged) the result is silence. No
-- setting, and nothing here names a card, a profile or an output: the rule
-- reads what each output reports. Loaded by
-- wireplumber.conf.d/60-cabinetos-sound-output.conf, which says what else the
-- rule needs.
--
-- WirePlumber ranks an analog output above HDMI and passes over one only when
-- it reports nothing plugged in ("no"). An output with no way to sense a plug
-- reports "unknown" and is never passed over: a speaker in the case (#547), a
-- line out on a card set up by a UCM profile (#655), a USB chip's jack (#341).
-- Each of those took the sound from the screen on some PC.
--
-- IT STARTS ONCE A SCREEN'S SOUND IS SEEN CONNECTED, AND STAYS ON FOR THE RUN.
-- Until then WirePlumber's own choice stands, so a PC with no screen sound
-- keeps its speakers or its jack: a laptop with no TV, a monitor that says it
-- has no sound. After that a TV going off still means silence. The run is
-- WirePlumber's, so it starts over with each start of the console's user.
--
-- Three steps, each between two of WirePlumber's own, named below. IF THE
-- BUILD STOPS IN build_files/sound-output-rule.sh, an update changed those.
--
-- 1. THE DEFAULT OUTPUT: a sound-card output that is not a screen's and does
--    not report a plug is taken out of the list before WirePlumber picks.
--    Left in: anything not on a sound card (Bluetooth, Remote Play's own
--    output) and a choice somebody made (Remote Play during a stream, Steam's
--    sound setting), which WirePlumber settles before this runs.
-- 2. THE SILENT OUTPUT. With nothing left in the list a program's sound would
--    not stop: with no default WirePlumber links it to the best output it can
--    find (linking/find-best-target.lua), which is the one left out. PipeWire's
--    own Dummy Output (#228, build_files/sound-backup-output.sh) appears only
--    when a PC has no other output at all, so the rule makes one the first
--    time it leaves anything out.
-- 3. THE CARD'S PROFILE. A card that has its jack and its screen on one
--    controller can run only one of them under the usual profile set, and
--    WirePlumber counts "unknown" as usable, so it keeps the speaker's and the
--    screen's output does not exist at all. Here a profile with something
--    reporting a plug comes first. A plug going in does not always change what
--    a profile offers, so each change of a plug looks again.

cutils = require ("common-utils")
putils = require ("profile-utils")

log = Log.open_topic ("s-cabinetos-sound-output")

rule_started = false
silent_output = nil

-- A card's outputs, as WirePlumber's own hooks read them.
function outputRoutes (device)
  local routes = {}
  for p in device:iterate_params ("EnumRoute") do
    local route = cutils.parseParam (p, "EnumRoute")
    if route and route.direction == "Output" then
      table.insert (routes, route)
    end
  end
  return routes
end

-- HDMI and DisplayPort share the type. The name is for a card set up by a UCM
-- profile, which calls its outputs HDMI1, HDMI2 and so on.
function isScreen (route)
  local info = route.info
  if type (info) == "table" then
    -- a count, then key and value pairs
    for i = 2, #info - 1, 2 do
      if info [i] == "port.type" and info [i + 1] == "hdmi" then
        return true
      end
    end
  end
  return string.find (string.lower (route.name or ""), "hdmi", 1, true) ~= nil
end

function ruleIsOn ()
  if rule_started then
    return true
  end
  for device in cutils.get_object_manager ("device"):iterate {
    Constraint { "device.api", "=", "alsa" },
  } do
    for _, route in ipairs (outputRoutes (device)) do
      if route.available == "yes" and isScreen (route) then
        rule_started = true
        -- At warning, the one level the journal keeps: ci/base-watch.txt and
        -- a player's report read this line.
        log:warning ("the sound rule started: " .. tostring (route.name) ..
            " is connected on " .. tostring (device.properties ["device.name"]))
        return true
      end
    end
  end
  return false
end

-- A sound-card output that is neither a screen's nor reporting a plug.
function isLeftOut (node_props)
  local device_id = node_props ["device.id"]
  if not device_id then
    return false
  end
  local device = cutils.get_object_manager ("device"):lookup {
    Constraint { "bound-id", "=", device_id, type = "gobject" },
  }
  if not device or device.properties ["device.api"] ~= "alsa" then
    return false
  end

  local card_profile_device = tonumber (node_props ["card.profile.device"])
  for _, route in ipairs (outputRoutes (device)) do
    if type (route.devices) == "table" then
      for _, d in ipairs (route.devices) do
        if d == card_profile_device and
            (route.available == "yes" or isScreen (route)) then
          return false
        end
      end
    end
  end
  return true
end

function makeSilentOutput ()
  if silent_output then
    return
  end
  silent_output = Node ("adapter", {
    ["factory.name"] = "support.null-audio-sink",
    ["node.name"] = "cabinetos-silent-output",
    ["node.description"] = "No sound (the screen's sound is gone)",
    ["media.class"] = "Audio/Sink",
    ["audio.position"] = "FL,FR",
  })
  silent_output:activate (Feature.Proxy.BOUND)
end

SimpleEventHook {
  name = "cabinetos/leave-out-outputs-with-no-plug",
  after = { "default-nodes/find-selected-default-node",
            "default-nodes/find-stored-default-node" },
  before = { "default-nodes/find-best-default-node" },
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-default-node" },
    },
  },
  execute = function (event)
    if event:get_properties () ["default-node.type"] ~= "audio.sink" or
        event:get_data ("selected-node") ~= nil or not ruleIsOn () then
      return
    end

    local available_nodes = event:get_data ("available-nodes")
    available_nodes = available_nodes and available_nodes:parse ()
    if not available_nodes then
      return
    end

    local kept = {}
    for _, node_props in ipairs (available_nodes) do
      if isLeftOut (node_props) then
        log:info ("left out " .. tostring (node_props ["node.name"]))
      else
        table.insert (kept, Json.Object (node_props))
      end
    end

    if #kept ~= #available_nodes then
      makeSilentOutput ()
      event:set_data ("available-nodes", Json.Array (kept))
    end
  end
}:register ()

SimpleEventHook {
  name = "cabinetos/profile-with-a-plug-first",
  after = { "device/find-stored-profile",
            "device/find-preferred-profile" },
  before = { "device/find-best-profile" },
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-profile" },
    },
  },
  execute = function (event)
    local device = event:get_subject ()
    if event:get_data ("selected-profile") or
        device.properties ["device.api"] ~= "alsa" or
        device.properties ["device.profile"] or not ruleIsOn () then
      return
    end

    local scores = putils.buildOutputRouteScores (outputRoutes (device))
    local best = nil
    for p in device:iterate_params ("EnumProfile") do
      local profile = cutils.parseParam (p, "EnumProfile")
      local score = profile and scores [tonumber (profile.index)]
      if score and score.yes > 0 and profile.name ~= "pro-audio" and
          (best == nil or putils.compareProfiles (profile, best, scores)) then
        best = profile
      end
    end

    if best then
      log:info (device, "profile with a plug: " .. best.name)
      event:set_data ("selected-profile", best)
    end
  end
}:register ()

SimpleEventHook {
  name = "cabinetos/look-again-when-a-plug-changes",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "device-params-changed" },
      Constraint { "event.subject.param-id", "=", "EnumRoute" },
    },
  },
  execute = function (event)
    local device = event:get_subject ()
    if device.properties ["device.api"] == "alsa" and ruleIsOn () then
      event:get_source ():call ("push-event", "select-profile", device, nil)
    end
  end
}:register ()
