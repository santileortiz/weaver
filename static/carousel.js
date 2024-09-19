class Carousel {
	constructor(carouselElement) {
		this.carousel = carouselElement;
		this.images = Array.from(this.carousel.querySelectorAll('img'));
		this.total = this.images.length;
		this.index = 0;

		this.setupCarousel();
		this.createIndicators();
		this.updateCarousel();
	}

	setupCarousel() {
		// Wrap images in a div.images
		this.images.forEach(img => {
			const imgContainer = document.createElement('div');
			imgContainer.classList.add('carousel-image-container');
			img.parentNode.insertBefore(imgContainer, img);
			imgContainer.appendChild(img);
		});

		this.imagesContainer = document.createElement('div');
		this.imagesContainer.classList.add('carousel-images');

		// Move image containers into imagesContainer
		this.images.forEach(img => {
			const imgContainer = img.parentNode;
			this.imagesContainer.appendChild(imgContainer);
		});

		this.carousel.appendChild(this.imagesContainer);

		// Create Prev and Next buttons
		this.prevButton = document.createElement('button');
		this.prevButton.classList.add('carousel-prev');
		this.prevButton.innerHTML = '&#10094;';
		this.prevButton.addEventListener('click', () => this.prev());

		this.nextButton = document.createElement('button');
		this.nextButton.classList.add('carousel-next');
		this.nextButton.innerHTML = '&#10095;';
		this.nextButton.addEventListener('click', () => this.next());

		this.carousel.appendChild(this.prevButton);
		this.carousel.appendChild(this.nextButton);
	}

	createIndicators() {
		this.indicatorContainer = document.createElement('div');
		this.indicatorContainer.classList.add('carousel-indicator');

		this.dots = [];

		for (let i = 0; i < this.total; i++) {
			const dot = document.createElement('span');
			dot.classList.add('carousel-dot');
			if (i === 0) dot.classList.add('carousel-dot-active');
			dot.addEventListener('click', () => this.goToImage(i));
			this.indicatorContainer.appendChild(dot);
			this.dots.push(dot);
		}

		this.carousel.appendChild(this.indicatorContainer);
	}

	updateIndicator() {
		this.dots.forEach((dot, i) => {
			dot.classList.toggle('carousel-dot-active', i === this.index);
		});
	}

	updateCarousel() {
		const maxWidth = parseInt(getComputedStyle(document.documentElement).getPropertyValue('--carousel-max-width'));
		this.imagesContainer.style.transform = `translateX(${-this.index * maxWidth}px)`;
		this.updateIndicator();
	}

	prev() {
		this.index = (this.index > 0) ? this.index - 1 : this.total - 1;
		this.updateCarousel();
	}

	next() {
		this.index = (this.index < this.total - 1) ? this.index + 1 : 0;
		this.updateCarousel();
	}

	goToImage(i) {
		this.index = i;
		this.updateCarousel();
	}
}

function carousel_init_in (html_element)
{
	const carousels = html_element.querySelectorAll('.carousel');
	carousels.forEach(carouselElement => new Carousel(carouselElement));
}
