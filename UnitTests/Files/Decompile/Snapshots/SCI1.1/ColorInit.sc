;;; Sierra Script 1.0 - (do not remove this comment)
(script# 12)
(include sci.sh)
(use Main)

(public
	ColorInit 0
)

(procedure (ColorInit)
	(= gColorWindowForeground 0)
	(= gLowlightColor (Palette 5 159 159 159))
	(= gColorWindowBackground 5)
)
