;;; Sierra Script 1.0 - (do not remove this comment)
(script# 969)
(include sci.sh)

; A &rest before the last argument of a message (Island of Dr. Brain script
; 268, anElement::select: "pushi 1; &rest 1; lst temp0; super"). The text
; keeps it at its place, with the name of its parameter: a &rest with no
; name would take the next argument as its name.
(public
	r2RestBeforeTheLastArgument 0
)

(procedure (r2RestBeforeTheLastArgument param1 param2 &tmp temp0)
	(asm
		pushi #posn
		push1
		&rest param2
		lst temp0
		lap param1
		send 6
		ret
	)
)
