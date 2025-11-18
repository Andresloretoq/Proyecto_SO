# Compilador
CC     = gcc
CFLAGS = -Wall -Wextra -pthread -std=c11

# Carpetas
SRCDIR  = src
BUILDDIR = build

# Ejecutables
CTRL = $(BUILDDIR)/controlador
AGT  = $(BUILDDIR)/agente

# Regla principal
all: $(BUILDDIR) $(CTRL) $(AGT)

# Crear carpeta build si no existe
$(BUILDDIR):
	mkdir -p $(BUILDDIR)

# Compilar controlador
$(CTRL): $(SRCDIR)/controlador.c
	$(CC) $(CFLAGS) $(SRCDIR)/controlador.c -o $(CTRL)

# Compilar agente
$(AGT): $(SRCDIR)/agente.c
	$(CC) $(CFLAGS) $(SRCDIR)/agente.c -o $(AGT)

# Limpiar
clean:
	rm -rf $(BUILDDIR)
