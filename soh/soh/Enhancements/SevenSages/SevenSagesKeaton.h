#ifndef SEVEN_SAGES_KEATON_H
#define SEVEN_SAGES_KEATON_H

/*
 * Seven Sages Keaton Mask — the one predicate that has to be shared.
 *
 * The mask's half-price discount is silent: the price tags simply read lower.
 * That is easy to miss and easy to doubt, so the shopkeeper says it out loud
 * when you talk to them wearing the mask (requested 2026-08-06).
 *
 * Saying it out loud means rewriting the shopkeeper's talk-to-owner line, and
 * the NPC hints feature (SevenSagesNpcHints.cpp) rewrites that same line. Both
 * are OnOpenText hooks, so without a rule between them the winner would be
 * whichever happened to be registered last. This predicate is that rule: the
 * mask claims the line, hints stand down while it is worn.
 *
 * Nothing is lost by it — take the mask off and the shopkeeper gives the hint
 * as usual. The greeting is a one-line confirmation you want on first contact;
 * the hint is seed information you want to keep. Neither needs the other's turn.
 */

#include <stdbool.h>

// True when the Keaton Mask is about to replace the shopkeeper's talk-to-owner
// line with its half-price greeting. Only ever true inside an OnOpenText hook,
// and only for an EnOssan shop owner mid-conversation.
bool SevenSagesKeatonClaimsShopGreeting();

#endif // SEVEN_SAGES_KEATON_H
