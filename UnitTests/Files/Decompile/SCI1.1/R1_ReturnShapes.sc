;;; Sierra Script 1.0 - (do not remove this comment)
(script# 927)
(include sci.sh)

; Return values. A function returns the accumulator, so the shape of the
; text is a judgement (see ReturnCleanup in DecompilerAstPasses.cpp).
; r1IfReturns: both branches return, and the final ret is reached by the
;   dead jmp after the then. Sierra: (if param1 (return 1) else (return 0)),
;   with no return around the if.
; r1ValueIf: the then leaves a number and the else a send; the final ret
;   returns whichever. Sierra: (return (if param1 1 else (param2 init:))).
; r1Increment: a ++ before the final ret is a statement, not a return value.
;   Sierra: (if param1 (param2 init:) else (++ temp0)).
(public
	r1IfReturns 0
	r1ValueIf 1
	r1Increment 2
)

(procedure (r1IfReturns param1)
	(asm
		lap param1
		bnt elseBranch1
		ldi 1
		ret
		jmp done1
	elseBranch1:
		ldi 0
		ret
	done1:
		ret
	)
)

(procedure (r1ValueIf param1 param2)
	(asm
		lap param1
		bnt elseBranch2
		ldi 1
		jmp done2
	elseBranch2:
		pushi #init
		pushi 0
		lap param2
		send 4
	done2:
		ret
	)
)

(procedure (r1Increment param1 param2 &tmp temp0)
	(asm
		lap param1
		bnt elseBranch3
		pushi #init
		pushi 0
		lap param2
		send 4
		jmp done3
	elseBranch3:
		+at temp0
	done3:
		ret
	)
)
