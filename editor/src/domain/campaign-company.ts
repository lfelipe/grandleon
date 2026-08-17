// SPDX-License-Identifier: MIT
import type { SourceProject } from "../generated/source-v1";

type SourceCampaign = NonNullable<SourceProject["campaigns"]>[number];
type CampaignFlow = NonNullable<SourceCampaign["flow"]>;
type CampaignNode = CampaignFlow["nodes"][number];

/**
 * Who is in a campaign's company, kept in step with who stands on its boards.
 *
 * Standing a character on the author's own side does two things at once: it
 * places them, and it **enrols them in the company**, because your side is
 * fought by the company and a placement there has to name somebody who carries
 * wounds and experience between Stages. `stage-cast.ts` writes both in one
 * transaction.
 *
 * Only one of the two was ever undone. Taking the placement off wrote the
 * node's placements and nothing else, so the member stayed — on no board,
 * invisible on the screen that had made them, and countable only on a console.
 * An author who built a Stage by trying arrangements and clearing them left one
 * behind every time. It was reported as a company of thirty-four where nineteen
 * stand on boards, on a console showing "1-7 of 34" in the corner.
 *
 * This is the other half of the gesture: a member the placement that enrolled
 * them no longer fields, and that no other board fields either, leaves.
 *
 * **It is safe to be this direct because there is one door.** `enrollMember` in
 * `ContentWorkspace.vue` is the only thing in this editor that adds a company
 * member, and it is called from the stamp. There is no hand-written roster to
 * be careful of. A member arriving from an imported project is left alone
 * unless the author takes their placement off, because the diff below only ever
 * names people whose placement this very save removed.
 */

/** Whom one node's placements field, by member id. */
export function membersFieldedByNode(
  node: CampaignNode | undefined
): ReadonlySet<string> {
  const fielded = new Set<string>();
  for (const placement of (node as { placements?: readonly {
    memberId?: string
  }[] } | undefined)?.placements ?? []) {
    if (placement.memberId !== undefined) fielded.add(placement.memberId);
  }
  return fielded;
}

/** Whom any board in a campaign fields, by member id. */
export function membersFieldedAnywhere(
  campaign: SourceCampaign
): ReadonlySet<string> {
  const fielded = new Set<string>();
  for (const node of campaign.flow?.nodes ?? []) {
    for (const member of membersFieldedByNode(node)) fielded.add(member);
  }
  return fielded;
}

/**
 * Company members no board fields at all.
 *
 * Reported rather than removed: this is what an author's *existing* project may
 * already be carrying, and quietly deleting people from a company on the next
 * save would be a second surprise on top of the first.
 */
export function unfieldedMembers(
  campaign: SourceCampaign
): readonly { readonly id: string; readonly name: string }[] {
  const fielded = membersFieldedAnywhere(campaign);
  return (campaign.roster ?? [])
    .filter((member) => !fielded.has(member.id))
    .map((member) => ({ id: member.id, name: member.name }));
}

/**
 * Takes out of the company anybody this save stopped fielding.
 *
 * Mutates the campaign draft, the way every other edit in the session does, and
 * answers with the names it took out so the surface can say so. Only members
 * `wereFielded` names are considered, so a save that changed nothing about who
 * stands where removes nobody, and a member no board ever fielded is left for
 * `unfieldedMembers` to report rather than deleted behind the author's back.
 */
export function unenrolMembersNoLongerFielded(
  campaign: SourceCampaign,
  wereFielded: ReadonlySet<string>
): readonly string[] {
  if (wereFielded.size === 0) return [];
  const stillFielded = membersFieldedAnywhere(campaign);
  const leaving = [...wereFielded].filter((id) => !stillFielded.has(id));
  if (leaving.length === 0) return [];

  const roster = campaign.roster ?? [];
  const names = roster
    .filter((member) => leaving.includes(member.id))
    .map((member) => member.name);
  if (names.length === 0) return [];

  const kept = roster.filter((member) => !leaving.includes(member.id));
  // An absent roster and an empty one say different things about a campaign,
  // and the format takes the absent one for a company nobody founded. Same
  // rule the workspace applies when it writes a roster at all.
  if (kept.length > 0) campaign.roster = kept;
  else delete campaign.roster;
  return names;
}
