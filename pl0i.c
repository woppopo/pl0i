#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

typedef enum
{
	Op_Invalid,
	Op_Load,
	Op_Literal,
	Op_Store,
	Op_Operate,
	Op_Allocate,
	Op_Jump,
	Op_JumpZero,
	Op_Call,
	Op_Intrinsic,
	Op_Label,
	Op_Return,
} OpKind;

typedef struct
{
	OpKind kind;
	int opr1;
	int opr2;
} Op;

typedef struct
{
	Op *ops;
	size_t size;
} Ops;

typedef struct
{
	size_t top;
	size_t size;
	int *data;
} Stack;

typedef struct
{
	const size_t static_link;
	const size_t dynamic_link;
	const size_t return_address;
} Record;

bool is_whitespace(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool is_blank_line(const char *str)
{
	size_t len = strlen(str);
	for (size_t i = 0; i < len; ++i)
	{
		if (!is_whitespace(str[i]))
		{
			return false;
		}
	}
	return true;
}

char *strdup_without_whitespace(const char *str)
{
	char *without_whitespace = (char *)malloc(strlen(str) + 1);
	for (size_t i = 0, j = 0; i <= strlen(str); i++)
	{
		if (!is_whitespace(str[i]))
		{
			without_whitespace[j] = str[i];
			j++;
		}
	}
	return without_whitespace;
}

size_t count_lines(FILE *fp)
{
	size_t lines = 0;

	int ch;
	while ((ch = fgetc(fp)) != EOF)
	{
		if (ch == '\n')
			lines++;
	}
	fseek(fp, 0, SEEK_SET);

	return lines + 1; // The last line doesn't contain '\n'
}

Stack stack_new(void)
{
	Stack stack;
	stack.top = 0;
	stack.size = 0;
	stack.data = (int *)malloc(0);
	return stack;
}

void stack_free(Stack stack)
{
	free(stack.data);
}

void stack_allocate(Stack *stack, size_t size)
{
	stack->top += size;
	stack->size += size;
	stack->data = (int *)realloc(stack->data, stack->size * sizeof(int));
}

int stack_get(const Stack *stack, size_t at)
{
	if (at > stack->top)
	{
		fprintf(stderr, "Invalid memory address: %lu\n", at);
		exit(EXIT_FAILURE);
	}

	return stack->data[at];
}

void stack_set(Stack *stack, size_t at, int value)
{
	if (at > stack->top)
	{
		fprintf(stderr, "Invalid memory address: %lu\n", at);
		exit(EXIT_FAILURE);
	}

	stack->data[at] = value;
}

void stack_push(Stack *stack, int value)
{
	stack_allocate(stack, 1);
	stack->data[stack->top - 1] = value;
}

int stack_pop(Stack *stack)
{
	int value = stack->data[stack->top - 1];
	stack->top--;
	return value;
}

Record get_record(const Stack *stack, size_t base_ptr)
{
	Record record = {
		 .static_link = (size_t)stack_get(stack, base_ptr),
		 .dynamic_link = (size_t)stack_get(stack, base_ptr + 1),
		 .return_address = (size_t)stack_get(stack, base_ptr + 2),
	};
	return record;
}

size_t push_record(Stack *stack, Record record)
{
	size_t base = stack->top;
	stack_push(stack, (int)record.static_link);
	stack_push(stack, (int)record.dynamic_link);
	stack_push(stack, (int)record.return_address);
	return base;
}

void pop_record(Stack *stack, size_t *pc, size_t *bp)
{
	Record record = get_record(stack, *bp);
	stack->top = *bp;
	*bp = record.dynamic_link;
	*pc = record.return_address;
}

Op parse_op(const char *line)
{
	Op op;
	char op_name[4];

	char *op_str = strdup_without_whitespace(line);
	sscanf(op_str, "(%[^,],%d,%d)", op_name, &op.opr1, &op.opr2);
	free(op_str);

	if (strcmp(op_name, "LOD") == 0 || strcmp(op_name, "lod") == 0)
	{
		op.kind = Op_Load;
	}
	else if (strcmp(op_name, "LIT") == 0 || strcmp(op_name, "lit") == 0)
	{
		op.kind = Op_Literal;
	}
	else if (strcmp(op_name, "STO") == 0 || strcmp(op_name, "sto") == 0)
	{
		op.kind = Op_Store;
	}
	else if (strcmp(op_name, "OPR") == 0 || strcmp(op_name, "opr") == 0)
	{
		op.kind = Op_Operate;
	}
	else if (strcmp(op_name, "INT") == 0 || strcmp(op_name, "int") == 0)
	{
		op.kind = Op_Allocate;
	}
	else if (strcmp(op_name, "JMP") == 0 || strcmp(op_name, "jmp") == 0)
	{
		op.kind = Op_Jump;
	}
	else if (strcmp(op_name, "JPC") == 0 || strcmp(op_name, "jpc") == 0)
	{
		op.kind = Op_JumpZero;
	}
	else if (strcmp(op_name, "CAL") == 0 || strcmp(op_name, "cal") == 0)
	{
		op.kind = Op_Call;
	}
	else if (strcmp(op_name, "CSP") == 0 || strcmp(op_name, "csp") == 0)
	{
		op.kind = Op_Intrinsic;
	}
	else if (strcmp(op_name, "LAB") == 0 || strcmp(op_name, "lab") == 0)
	{
		op.kind = Op_Label;
	}
	else if (strcmp(op_name, "RET") == 0 || strcmp(op_name, "ret") == 0)
	{
		op.kind = Op_Return;
	}
	else
	{
		fprintf(stderr, "Unknown op: %s\n", op_name);
		exit(EXIT_FAILURE);
	}

	return op;
}

Ops parse(FILE *fp)
{
	// Count lines in a file to determine allocation size of opcodes and labels
	size_t lines_count = count_lines(fp);

	// Labels: Mapping of operand values to instruction addresses
	size_t *labels = (size_t *)malloc(lines_count * sizeof(size_t));

	// Opcodes: This is not shared with RAM. It can be considered as Harvard architecture.
	Op *ops = (Op *)malloc(lines_count * sizeof(Op));
	size_t ops_count = 0;

	char line[20];
	while (fgets(line, sizeof(line) / sizeof(char), fp) != NULL)
	{
		// Don't parse when it is blank line
		if (is_blank_line(line))
		{
			continue;
		}

		Op op = parse_op(line);

		// Label maps an operand value (opr2) of a jump instruction
		// to its own instruction address
		if (op.kind == Op_Label)
		{
			labels[op.opr2] = ops_count;
		}

		ops[ops_count] = op;
		ops_count++;
	}

	// Convert an operand value of a jump instruction
	// to an corresponding instruction number.
	for (size_t i = 0; i < ops_count; ++i)
	{
		Op *op = &ops[i];
		if (op->kind == Op_Jump || op->kind == Op_JumpZero || op->kind == Op_Call)
		{
			op->opr2 = (int)labels[op->opr2];
		}
	}
	free(labels);

	return (Ops){
		 .ops = ops,
		 .size = ops_count,
	};
}

size_t base(const Stack *stack, size_t base_ptr, size_t level_diff)
{
	if (level_diff == 0)
	{
		return base_ptr;
	}
	else
	{
		Record record = get_record(stack, base_ptr);
		return base(stack, record.static_link, level_diff - 1);
	}
}

size_t value_at(const Stack *stack, size_t base_ptr, size_t level_diff, int offset)
{
	size_t base_addr = base(stack, base_ptr, level_diff);
	return (size_t)((int)base_addr + offset);
}

void run(Ops ops)
{
	Record initial_record = {
		 .static_link = 0,
		 .dynamic_link = 0,
		 .return_address = 0,
	};

	Stack stack = stack_new();
	size_t pc = 0;
	size_t bp = push_record(&stack, initial_record);

	while (stack.top != 0)
	{
		if (pc >= ops.size)
		{
			fprintf(stderr, "PC out of bounds: %lu\n", pc);
			exit(EXIT_FAILURE);
		}

		Op op = ops.ops[pc];
		switch (op.kind)
		{
		case Op_Invalid:
		{
			printf("Invalid op at memory %ld\n", pc);
			exit(EXIT_FAILURE);
			break;
		}
		case Op_Load:
		{
			size_t at = value_at(&stack, bp, (size_t)op.opr1, op.opr2);
			int value = stack_get(&stack, at);
			stack_push(&stack, value);
			pc++;
			break;
		}
		case Op_Literal:
		{
			stack_push(&stack, op.opr2);
			pc++;
			break;
		}
		case Op_Store:
		{
			size_t at = value_at(&stack, bp, (size_t)op.opr1, op.opr2);
			int value = stack_pop(&stack);
			stack_set(&stack, at, value);
			pc++;
			break;
		}
		case Op_Operate:
			switch (op.opr2)
			{
			case 0:
			{
				pop_record(&stack, &pc, &bp);
				break;
			}
			case 2:
			{
				int b = stack_pop(&stack);
				int a = stack_pop(&stack);
				stack_push(&stack, a + b);
				break;
			}
			case 3:
			{
				int b = stack_pop(&stack);
				int a = stack_pop(&stack);
				stack_push(&stack, a - b);
				break;
			}
			case 4:
			{
				int b = stack_pop(&stack);
				int a = stack_pop(&stack);
				stack_push(&stack, a * b);
				break;
			}
			case 5:
			{
				int b = stack_pop(&stack);
				int a = stack_pop(&stack);
				stack_push(&stack, a / b);
				break;
			}
			case 6:
			{
				stack_push(&stack, stack_pop(&stack) % 2 == 1 ? 1 : 0);
				break;
			}
			case 8:
			{
				int b = stack_pop(&stack);
				int a = stack_pop(&stack);
				stack_push(&stack, a == b ? 1 : 0);
				break;
			}
			case 9:
			{
				int b = stack_pop(&stack);
				int a = stack_pop(&stack);
				stack_push(&stack, a != b ? 1 : 0);
				break;
			}
			case 10:
			{
				int b = stack_pop(&stack);
				int a = stack_pop(&stack);
				stack_push(&stack, a < b ? 1 : 0);
				break;
			}
			case 11:
			{
				int b = stack_pop(&stack);
				int a = stack_pop(&stack);
				stack_push(&stack, a >= b ? 1 : 0);
				break;
			}
			case 12:
			{
				int b = stack_pop(&stack);
				int a = stack_pop(&stack);
				stack_push(&stack, a > b ? 1 : 0);
				break;
			}
			case 13:
			{
				int b = stack_pop(&stack);
				int a = stack_pop(&stack);
				stack_push(&stack, a <= b ? 1 : 0);
				break;
			}
			default:
			{
				fprintf(stderr, "Unknown opr2: %d\n", op.opr2);
				exit(EXIT_FAILURE);
			}
			}
			pc++;
			break;
		case Op_Allocate:
		{
			stack_allocate(&stack, (size_t)op.opr2);
			pc++;
			break;
		}
		case Op_Jump:
		{
			pc = (size_t)op.opr2;
			break;
		}
		case Op_JumpZero:
		{
			if (stack_pop(&stack) == 0)
			{
				pc = (size_t)op.opr2;
			}
			else
			{
				pc++;
			}
			break;
		}
		case Op_Call:
		{
			// Set the record with the specified level difference as a static link
			size_t target_bp = base(&stack, bp, (size_t)op.opr1);
			Record target_record = get_record(&stack, target_bp);
			size_t static_link = target_record.static_link;

			Record new_record = {
				 .static_link = static_link,
				 .dynamic_link = bp,
				 .return_address = pc + 1,
			};

			bp = push_record(&stack, new_record);
			pc = (size_t)op.opr2;
			break;
		}
		case Op_Intrinsic:
			switch (op.opr2)
			{
			case 0:
			{
				// Input a number and push it to the stack.
				int value;
				printf("Input: ");
				scanf("%d", &value);
				stack_push(&stack, value);
				break;
			}
			case 1:
			{
				// Pop a value from the stack and print it.
				printf("Output: %d", stack_pop(&stack));
				break;
			}
			case 2:
			{
				// Print a new line.
				printf("\n");
				break;
			}
			}
			pc++;
			break;
		case Op_Label:
		{
			// noop
			pc++;
			break;
		}
		case Op_Return:
		{
			// Return to the caller,
			// remove the arguments when the callee was called,
			// and push the return value.
			int ret = stack_pop(&stack);
			pop_record(&stack, &pc, &bp);
			stack.top -= (size_t)op.opr2;
			stack_push(&stack, ret);
			break;
		}
		}
	}

	stack_free(stack);
	free(ops.ops);
}

int main(int argc, char **argv)
{
	if (argc != 2)
	{
		printf("Usage: %s <filename>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	FILE *fp = fopen(argv[1], "r");
	if (fp == NULL)
	{
		printf("Error: cannot open file %s\n", argv[1]);
		exit(EXIT_FAILURE);
	}

	Ops ops = parse(fp);
	fclose(fp);

	run(ops);
	exit(EXIT_SUCCESS);
}
