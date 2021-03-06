final class GalleryCell: UICollectionViewCell {
  typealias Model = GalleryItemModel

  @IBOutlet weak var imageView: UIImageView!

  var model: Model! {
    didSet {
      imageView.wi_setImage(with: model.imageURL, transitionDuration: kDefaultAnimationDuration)
    }
  }

  override func prepareForReuse() {
    super.prepareForReuse()
    imageView.wi_cancelImageRequest()
    imageView.image = nil;
  }
}
