;;; Sierra Script 1.0 - (do not remove this comment)
(script# 956)
(include sci.sh)

; Dead jmps that Sierra's compiler leaves after the jmp of a then-part: a
; break that no path runs. As a statement, the then-part would end with a
; break.
(public
	d1DeadBreak 0
	d1TwoDeadBreaks 1
	d1DeadBreakAfterExit 2
)

(local
	local0
	local1
	local2
	local3
)

; (while local0 (if local1 (= local1 1) else (break)) (= local2 2))
(procedure (d1DeadBreak)
	(asm
	head1:
		lal local0
		bnt exit1
		lal local1
		bnt exit1
		ldi 1
		sal local1
		jmp join1
		jmp exit1
	join1:
		ldi 2
		sal local2
		jmp head1
	exit1:
		ret
	)
)

; The same, with two dead jmps.
(procedure (d1TwoDeadBreaks)
	(asm
	head2:
		lal local0
		bnt exit2
		lal local1
		bnt exit2
		ldi 1
		sal local1
		jmp join2
		jmp exit2
		jmp exit2
	join2:
		ldi 2
		sal local2
		jmp head2
	exit2:
		ret
	)
)

; (repeat (if local0 (if local1 (= local1 1) else (break)) else (= local2 2))
; (= local3 3)): the dead break comes after the jmp of the inner then-part.
(procedure (d1DeadBreakAfterExit)
	(asm
	head3:
		lal local0
		bnt else03
		lal local1
		bnt exit3
		ldi 1
		sal local1
		jmp join03
		jmp exit3
		jmp join03
	else03:
		ldi 2
		sal local2
	join03:
		ldi 3
		sal local3
		jmp head3
	exit3:
		ret
	)
)
