;;; Sierra Script 1.0 - (do not remove this comment)
(script# 942)
(include sci.sh)

; A loop whose body starts with a switch, so the switch is the loop's head.
; Each if in the body ends with a "bnt" or a "jmp" straight to the loop head,
; and a dead "jmp" to the head follows each break. The fixup of folded loop
; exits sends the "bnt" of the first if to the first dead "jmp", which is in
; the else of the cond. That "jmp" is a latch trampoline: it must fold into
; the common latch, also when the loop's head is a switch. Shape from
; SRDialog::doit (script 990) of many SCI0 games.
(public
	f18SwitchHeadContinue 0
)

(procedure (f18SwitchHeadContinue param1 param2 param3 &tmp temp0 temp1)
	(asm
	loopHead:
		lsp param1
		dup
		ldi 0
		eq?
		bnt case1
		ldi 5
		jmp switchEnd
	case1:
		ldi 6
	switchEnd:
		toss
		sat temp0
		lst temp0
		ldi 5
		eq?
		bnt elseBranch
		lap param2
		bnt loopHead
		ldi 1
		sat temp1
		jmp loopHead
	elseBranch:
		lst temp0
		ldi 6
		eq?
		bnt clause3
		lsp param1
		ldi 2
		eq?
		bnt clause3
		lap param3
		bnt loopHead
		ldi 2
		sat temp1
		jmp loopExit
		jmp loopHead
	clause3:
		lap param3
		bnt loopHead
		ldi 3
		sat temp1
		jmp loopExit
		jmp loopHead
	loopExit:
		lat temp1
		ret
	)
)