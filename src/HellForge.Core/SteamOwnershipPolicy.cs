namespace HellForge.Core;

// Evaluate only observations obtained directly from a verified Steam client API probe.
// A serialized result is evidence, not a reusable authorization token.
public sealed record SteamOwnershipObservation(uint AppId, bool LoggedOn, bool Subscribed, bool SameOwner,
    bool FamilyShared, bool FreeWeekend, bool Cafe);

// The signed-in account owns the game, or plays it through Steam Family Sharing: a licence Steam grants a family member,
// whose app owner is then the family member who bought it. Free weekends and cybercafé licences stay refused.
public static class SteamOwnershipPolicy
{
    public static bool ConfirmsCurrentClientOwner(SteamOwnershipObservation value) =>
        value.AppId == 553850 && value.LoggedOn && value.Subscribed && (value.SameOwner || value.FamilyShared) &&
        !value.FreeWeekend && !value.Cafe;
}
