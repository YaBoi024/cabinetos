// The battery row's measures (#293): its two halves use them (batteryrow.cpp),
// and the bar leaves kToName before the name (bar.cpp). Moved out of the
// bar's draw as they were (#398, restructure batch d).

#pragma once

#include "text.h"

// THE NAME'S SIZE, AND A BATTERY AS HEAVY AS ITS LETTERS. A step
// down (Caption1, a 34x18 outline of 2) was tried first and
// MMagTech, on the TV: it "just doesn't look right" beside the
// name and the destinations (2026-10-08).
const ui::TextStyle ls = ui::TextStyle::Callout;
constexpr float kToName = 36.0f, kBetween = 36.0f;
constexpr float kBodyW = 44.0f, kBodyH = 23.0f, kNubW = 4.0f, kNubH = 9.0f;
constexpr float kInset = 5.0f, kSegGap = 2.5f;
constexpr float kBoltGap = 7.0f, kBoltW = 13.0f, kBoltH = 23.0f;

// FORK (handheld): each label sits above its own battery, in the smaller
// caption size, so the row reads as small stacks (Deck, P1, P2) and stays
// inside the bar's height. A pad with no reading is its label alone.
const ui::TextStyle kRowLabel = ui::TextStyle::Caption1;
constexpr float kLabelAbove = 3.0f;   // between the label and its battery
constexpr float kBatteryW = kBodyW + 1.0f + kNubW;   // the body and its nub
