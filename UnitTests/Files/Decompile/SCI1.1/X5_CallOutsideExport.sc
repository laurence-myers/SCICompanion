;;; Sierra Script 1.0 - (do not remove this comment)
(script# 959)
(include sci.sh)

; Calls export 1 of X3_StaleExport. The test moves that export outside the
; code of script 964, so the call is to an export with no procedure.
(use X3_StaleExport)

(public
	callOutside 0
)

(procedure (callOutside)
	(staleSecond)
)
