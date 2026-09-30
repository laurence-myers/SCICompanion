;;; Sierra Script 1.0 - (do not remove this comment)
(script# 953)
(include sci.sh)
(use Main)
(use System)

; Objects whose names have a dot, as the Hoyle games have ("game.opt"). The
; decompiler names a global, a local and a temp from them. A dot is not valid
; in a variable name.
(public
	dottedObjectNames 0
)

(local
	local0
)

(procedure (dottedObjectNames &tmp temp0)
	(= global5 gameOpt)
	(= local0 saveSol)
	(= temp0 menuOpt)
	(temp0 dispose:)
	(local0 dispose:)
)

(instance gameOpt of Code
	(properties
		name "game.opt"
	)
)

(instance saveSol of Code
	(properties
		name "save.sol"
	)
)

(instance menuOpt of Code
	(properties
		name "menu.opt"
	)
)
