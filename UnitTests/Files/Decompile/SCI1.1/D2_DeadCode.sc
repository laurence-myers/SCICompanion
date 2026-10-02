;;; Sierra Script 1.0 - (do not remove this comment)
(script# 958)
(include sci.sh)

; Dead code that is not right after a ret: as statements, the text would run
; it. Shape of PQ1 useLock::changeState.
(public
	d2DeadAfterJmp 0
	d2DeadAfterBreak 1
)

(local
	local0
	local1
	local2
	local3
)

; (= local1 1) (= local3 2): the jmp skips a dead store.
(procedure (d2DeadAfterJmp)
	(asm
		ldi 1
		sal local1
		jmp next1
		ldi 9
		sal local2
	next1:
		ldi 2
		sal local3
		ret
	)
)

; (while local0 (if local1 (break)) (= local3 3)): a dead store after the
; break.
(procedure (d2DeadAfterBreak)
	(asm
	head2:
		lal local0
		bnt exit2
		lal local1
		bnt skip2
		jmp exit2
		ldi 9
		sal local2
	skip2:
		ldi 3
		sal local3
		jmp head2
	exit2:
		ret
	)
)
