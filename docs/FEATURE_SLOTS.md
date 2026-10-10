# Saved feature slots

Existing feature IDs are never compacted. Slots 0–14 retain their identities;
15–19 remain unknown/reserved; Camera uses 20/21; Resource 22 stays reserved;
InventoryHUD 23 is retired/reserved; AntiEffect uses 24; Inspector 25 stays
reserved; OffhandSwap uses 26; 27–31 remain unknown/reserved. Old enabled=true
for slot 23 cannot resurrect InventoryHUD. Unknown JSON and saved key bindings
are preserved by the existing codec. Config files are not deleted during this
UI change. HUD layout controls and the selected GUI host remain available.
