;;; Sierra Script 1.0 - (do not remove this comment)
(script# 933)
(include sci.sh)

; Sierra's shape for (<= a (b x?) c): "a; push; (b x?); le?; bnt END; pprev;
; c; le?; END:", with a variable as the last operand (QfG4 DText::handleEvent,
; Controls). The bnt is neutralized only when the compare after the pprev is
; the same operator as the one before the bnt.
; Sierra: (if (<= param2 (param1 x?) param3) (= temp0 1)) (return temp0)
(public
	n2SierraChainedCompare 0
)

(procedure (n2SierraChainedCompare param1 param2 param3 &tmp temp0)
	(asm
		lsp param2
		pushi #x
		push0
		lap param1
		send 4
		le?
		bnt fail
		pprev
		lap param3
		le?
		bnt fail
		ldi 1
		sat temp0
	fail:
		lat temp0
		ret
	)
)
