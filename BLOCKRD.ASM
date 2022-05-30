.MODEL compact

.CODE

; the function prototype is
; int bigread(int drive, u32 lba, u16 count, far void * buffer);
; which matches the DOS 4.x+ absolute read/write control block format,
; and thus we dont need to have a seperate control block :)
; bp frame values thus:
; drive        4
; lba low      6
; lba high     8
; count       10
; buf offset  12
; buf segment 14

GLOBAL C bigread:PROC
bigread PROC
	push bp
	mov bp, sp
	push ds
	mov ah, 30h
	int 21h
	cmp al, 4
	jl olddos
	mov ax, ss
	mov ds, ax
	mov bx, bp
	add bx, 6
	mov al, [bp+4]
	mov cx, 0FFFFh
	int 25h
	pop bx ; that int leave the flags on the stack, eww
	jc errv
	xor ax, ax
	pop ds
	pop bp
	ret

errv:	test ax, ax
	jnz errk
	not ax
errk:	pop ds
	pop bp
	ret

olddos:
	mov ax, [bp+14]
	mov ds, ax
	mov al, [bp+4]
	mov dx, [bp+6]
	mov cx, [bp+10]
	mov bx, [bp+12]
	int 25h
	pop bx ; bug-for-bug ..
	jc errv
	xor ax, ax
	pop ds
	pop bp
	ret
bigread ENDP




END